#include "avengine/vector.h"
#include "index.h"
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

  void* bumpAllocate(size_t length) const
  {
    uint8_t* start = bumpAllocator;
    start += length;
    return start;
  }

  Index()
      : buckets(new PostingList*[HASHMAP_SIZE]()),
        bumpAllocator((uint8_t*)malloc((size_t)32768 * 1048576))
  {
  }

  uint32_t beginDocument(const char* url)
  {
    documentNames.pushBack(url);
    offset = 0;
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

  twoWayMerge(argv[2], argv[3]);
}
