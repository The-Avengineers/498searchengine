#include "avengine/vector.h"
#include "index.h"
#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

void parseKjvVerses(char* content, avengine::vector<char*>& documents,
                    avengine::vector<char*>& documentNames)
{
  char* verse = strtok(content, "\n");

  while (verse)
  {
    documentNames.pushBack(verse);

    while (*verse != '\t')
      verse++;

    *(verse++) = '\0';

    documents.pushBack(verse);

    verse = strtok(nullptr, "\n");
  }
}

char* memoryMapFile(char const* filename, size_t& length)
{
  int fd = open(filename, O_RDONLY);
  struct stat sb;
  if (fd == -1)
    return nullptr;

  assert(fstat(fd, &sb) != -1);

  char* data =
      static_cast<char*>(mmap(NULL, sb.st_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0));

  length = sb.st_size;

  // This might cause undefined behavior if it hits a new page.
  data[length] = '\0';

  return data;
}

size_t djb2Hash(char const* str)
{
  size_t hash = 5381;
  int c;

  while ((c = *str++))
    hash = ((hash << 5) + hash) + c; /* hash * 33 + c */

  return hash;
}

// Consider switching from AoS to SoA.
struct Posting
{
  uint32_t docId;
  uint32_t termFreq;
  // It might make sense to make the just an index if resizing is often changing
  // the base pointer location.
  uint32_t* positions;
};

struct PositionBlock
{
  PositionBlock* next;
  // The total size of the positions array, DOES NOT include the next and
  // capacity fields.
  size_t capacity;
  uint32_t positions[];
};

#define STARTING_CAPACITY 64

struct PostingList
{
  PostingList(char const* word, PostingList* nextBucket)
  {
    this->word = word;
    this->nextBucket = nextBucket;
    capacity = 4096;
    length = 0;

    postings = (Posting*)malloc(this->capacity * sizeof(Posting));
    allocNewBlock(STARTING_CAPACITY);
    headBlock = positionsStart;
  }

  void allocNewBlock(size_t capacity)
  {
    if (positionsStart)
      positionsStart = positionsStart->next =
          (PositionBlock*)malloc(sizeof(PositionBlock) + sizeof(uint32_t) * capacity);
    else
      positionsStart = (PositionBlock*)malloc(sizeof(PositionBlock) + sizeof(uint32_t) * capacity);

    positionsStart->next = nullptr;
    positionsStart->capacity = capacity;
    totalPositionCapacity += capacity;
    positionHead = positionsStart->positions;
    positionsEnd = &positionsStart->positions[capacity];
  }

  void ingest(uint32_t docId, size_t offset)
  {
    if (!length || postings[length - 1].docId != docId)
    {
      if (length >= capacity)
      {
        capacity *= 2;
        postings = (Posting*)realloc((void*)postings, capacity * sizeof(Posting));
      }

      postings[length].docId = docId;
      postings[length].positions = positionHead;
      postings[length++].termFreq = 0;
    }

    Posting& posting = postings[length - 1];

    posting.termFreq++;

    if (positionHead >= positionsEnd)
    {
      allocNewBlock(totalPositionCapacity);
    }

    *(positionHead++) = offset;
  }

  char const* word;
  PostingList* nextBucket;
  size_t capacity;
  size_t length;
  Posting* postings;

  PositionBlock* positionsStart = nullptr;
  PositionBlock* headBlock;
  size_t totalPositionCapacity = 0;
  uint32_t* positionHead;
  uint32_t* positionsEnd;
};

#define HASHMAP_SIZE 131072

// TODO: Actually write a variable length integers instead
uint8_t* write_var_uint32(uint8_t* buffer, uint32_t value)
{
  while (value >= 0x80u)
  {
    *buffer++ = static_cast<uint8_t>((value & 0x7Fu) | 0x80u);
    value >>= 7;
  }
  *buffer++ = static_cast<uint8_t>(value);
  return buffer;
}

uint8_t* write_uint64(uint8_t* buffer, uint64_t value)
{
  memcpy(buffer, &value, sizeof(uint64_t));
  return buffer + sizeof(uint64_t);
}

struct Index
{
  PostingList** buckets;
  size_t offset = 0;
  size_t totalUniqWords = 0;
  avengine::vector<const char*> documentNames;
  uint8_t* bumpAllocator;

  PostingList*& getBucket(char const* word)
  {
    size_t index = djb2Hash(word) % 4096;
    PostingList*& bucket = buckets[index];

    while (bucket && strcmp(bucket->word, word))
      bucket = bucket->nextBucket;

    if (!bucket)
      bucket = new PostingList(word, nullptr);

    return bucket;
  }

  void* bumpAllocate(size_t length)
  {
    uint8_t* start = bumpAllocator;
    bumpAllocator += length;
    return start;
  }

  Index()
      : buckets(new PostingList*[HASHMAP_SIZE]()),
        bumpAllocator((uint8_t*)malloc((size_t)32768 * 1048576))
  {
  }

  // Write the index structure to disk.
  void serialize(int fd)
  {
    size_t needed = 2;

    needed += (totalUniqWords + 1) * 2 * 2;

    uint64_t* dictionary_buffer = new uint64_t[needed]();
    std::fill(dictionary_buffer, dictionary_buffer + needed, UINT64_MAX);

    dictionary_buffer[0] = documentNames.size();
    dictionary_buffer[1] = totalUniqWords + 1;

    avengine::vector<char> string_buffer;

    for (size_t i = 0; i < HASHMAP_SIZE; i++)
    {
      PostingList* bucket = buckets[i];

      while (bucket)
      {
        size_t hash = djb2Hash(bucket->word) % (totalUniqWords * 2);

        // Find an empty bucket
        while (dictionary_buffer[2 + hash * 2 + 1] > 0)
        {
          hash += 1;
        }

        dictionary_buffer[2 + hash * 2] = string_buffer.size();

        // Add the term to the string buffer
        for (char const* str = bucket->word; *str; str++)
          string_buffer.pushBack(*str);

        string_buffer.pushBack('\0');

        bucket->word = (const char*)hash;
        bucket = bucket->nextBucket;
      }
    }

    // TODO: maybe fallocate?
    lseek(fd, needed * sizeof(uint64_t), SEEK_SET);

    write(fd, string_buffer.begin(), string_buffer.size());

    size_t postingsOffset = 0;

    // Allocate a buffer big buffer for writing, this should be changed to just what we need.
    uint8_t* posting_buffer = new uint8_t[262144]();
    uint8_t* posting_buffer_head = posting_buffer;

    // Now we write out the posting list
    for (size_t i = 0; i < HASHMAP_SIZE; i++)
    {
      PostingList* bucket = buckets[i];

      while (bucket)
      {
        size_t hash = (size_t)bucket->word;

        dictionary_buffer[2 + hash * 2 + 1] = postingsOffset;

        posting_buffer_head = write_uint64(posting_buffer_head, bucket->length);
        // TODO: Write total collection frequency
        posting_buffer_head = write_uint64(posting_buffer_head, 0xdeadbeef);
        PositionBlock* head = bucket->positionsStart;

        size_t lastPosition = 0;

        // TODO: Write index synchronization points and skip pointers

        // Write posts
        while (head)
        {
          // If we have a next pointer it means this block is full so we write all of it.
          if (head->next)
          {
            for (size_t i = 0; i < head->capacity; i++)
            {
              posting_buffer_head =
                  write_var_uint32(posting_buffer_head, head->positions[i] - lastPosition);
            }
          }
          else
          {
            for (size_t i = 0; i < bucket->positionsEnd - head->positions; i++)
            {
              posting_buffer_head =
                  write_var_uint32(posting_buffer_head, head->positions[i] - lastPosition);
            }
          }

          head = head->next;
        }

        postingsOffset += posting_buffer_head - posting_buffer;

        write(fd, posting_buffer, posting_buffer_head - posting_buffer);

        posting_buffer_head = posting_buffer;

        bucket = bucket->nextBucket;
      }
    }

    lseek(fd, 0, SEEK_SET);
    write(fd, dictionary_buffer, needed * sizeof(uint64_t));
  }

  uint32_t beginDocument(const char* url)
  {
    documentNames.pushBack(url);
    return documentNames.size() - 1;
  }

  // TODO: ingest should only take the word and get the `doc_id` from the
  // `Index`.
  void ingest(uint32_t docId, char const* word)
  {
    size_t index = djb2Hash(word) % HASHMAP_SIZE;
    PostingList* bucket = buckets[index];

    while (bucket && strcmp(bucket->word, word))
      bucket = bucket->nextBucket;

    if (!bucket)
    {
      totalUniqWords++;
      bucket = buckets[index] = new PostingList(word, buckets[index]);
    }

    bucket->ingest(docId, offset++);
  }
};

Index globalIndex;

size_t indexDocument(char const* url, char* pharses)
{
  uint32_t docId = globalIndex.beginDocument(url);

  while (*pharses && !isalpha(*pharses))
    pharses++;

  while (*pharses)
  {
    char const* word = pharses;

    while (*pharses && isalpha(*pharses))
    {
      *pharses = *pharses | 32;
      pharses++;
    }

    *(pharses++) = '\0';

    globalIndex.ingest(docId, word);

    while (*pharses && !isalpha(*pharses))
      pharses++;
  }

  return 0;
}

void twoWayMerge(const char* firstWord, const char* secondWord)
{
  PostingList* firstBucket = globalIndex.getBucket(firstWord);
  PostingList* secondBucket = globalIndex.getBucket(secondWord);

  size_t firstIndex = 0;
  size_t secondIndex = 0;

  while (firstIndex < firstBucket->length && secondIndex < secondBucket->length)
  {
    uint32_t firstId = firstBucket->postings[firstIndex].docId;
    uint32_t secondId = secondBucket->postings[secondIndex].docId;

    if (firstId > secondId)
    {
      secondIndex++;
    }
    else if (firstId < secondId)
    {
      firstIndex++;
    }
    else
    {
      printf("%s\n", globalIndex.documentNames[firstId]);
      firstIndex++;
      secondIndex++;
    }
  }
}

void inspectBucket(PostingList* bucket)
{
  for (size_t i = 0; i < bucket->length; i++)
  {
    printf("%s :", globalIndex.documentNames[bucket->postings[i].docId]);

    for (size_t j = 0; j < bucket->postings[i].termFreq; j++)
      printf(" %d", bucket->postings[i].positions[j]);

    printf("\n");
  }
}

void dumpWords(Index const& index)
{
  size_t amounts[8] = {0};

  for (size_t i = 0; i < HASHMAP_SIZE; i++)
  {
    PostingList* bucket = index.buckets[i];
    size_t j = 0;

    while (bucket)
    {
      j++;
      bucket = bucket->nextBucket;
    }

    if (j < 8)
      amounts[j]++;
    else
      amounts[7]++;
  }

  printf("%ld empty buckets\n", amounts[0]);

  for (size_t i = 1; i < 8; i++)
  {
    printf("%ld with %ld buckets\n", amounts[i], i);
  }
}

int main(int argc, char** argv)
{
  size_t length;
  char* content = memoryMapFile(argv[1], length);

  printf("Done mapping file!\n");

  avengine::vector<char*> documents;
  avengine::vector<char*> documentNames;
  parseKjvVerses(content, documents, documentNames);

  printf("Done parsing verses file, %ld documents!\n\e[?25l", documents.size());

  clock_t start = clock(), diff;
  for (size_t i = 0; i < documents.size(); i++)
  {
    if (i % 50 == 0)
    {
      printf("\e[2K%.2f%%\r", (float)i / (float)documents.size() * 100.0);
      fflush(stdout);
    }
    indexDocument(documentNames[i], documents[i]);
  }

  printf("\e[2K100.00%%\r");

  diff = clock() - start;

  int msec = diff * 1000 / CLOCKS_PER_SEC;
  printf("\e[?25h\nDone building index in %d seconds %d milliseconds\n", msec / 1000, msec % 1000);

  int fd = open("index.sfi", O_WRONLY | O_CREAT, 0644);
  globalIndex.serialize(fd);
  close(fd);
}
