#include "index.h"
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
      static_cast<char*>(mmap(nullptr, sb.st_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0));

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

PostingList::PostingList(char const* word, PostingList* nextBucket)
{
  this->word = word;
  this->nextBucket = nextBucket;
  capacity = 4096;
  length = 0;

  postings = (Posting*)malloc(this->capacity * sizeof(Posting));
}

void PostingList::ingest(uint32_t docId, size_t offset)
{
  if (!length || postings[length - 1].docId != docId)
  {
    if (length >= capacity)
    {
      capacity *= 2;
      postings = (Posting*)realloc((void*)postings, capacity * sizeof(Posting));
    }

    postings[length].docId = docId;
    postings[length].positions = positions.size() - 1;
    postings[length++].termFreq = 0;
  }

  Posting& posting = postings[length - 1];

  posting.termFreq++;
  positions.pushBack(offset);
}

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

PostingList*& Index::getBucket(char const* word)
{
  size_t index = djb2Hash(word) % 4096;
  PostingList*& bucket = buckets[index];

  while (bucket && strcmp(bucket->word, word))
    bucket = bucket->nextBucket;

  if (!bucket)
    bucket = new PostingList(word, nullptr);

  return bucket;
}

Index::Index() : buckets(new PostingList*[HASHMAP_SIZE]()) {}

void Index::serialize(int fd)
{
  size_t needed = 2;

  needed += (totalUniqWords + 1) * 2 * 2;

  uint64_t* dictionary_buffer = new uint64_t[needed]();
  // std::fill(dictionary_buffer, dictionary_buffer + needed, UINT64_MAX);

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
      while (dictionary_buffer[2 + hash * 2] > 0)
      {
        hash += 1;
        hash %= (totalUniqWords * 2);
      }

      dictionary_buffer[2 + hash * 2] = string_buffer.size();

      // Add the term to the string buffer
      for (char const* str = bucket->word; *str; str++)
        string_buffer.pushBack(*str);

      string_buffer.pushBack('\0');

      bucket->hash = hash;
      bucket = bucket->nextBucket;
    }
  }

  // TODO: maybe fallocate?
  lseek(fd, needed * sizeof(uint64_t), SEEK_SET);

  write(fd, string_buffer.begin(), string_buffer.size());

  size_t postingsOffset = 0;

  // Allocate a buffer big buffer for writing, this should be changed to just what we need.
  uint8_t* posting_buffer = new uint8_t[16777216]();
  uint8_t* postingBufferHead = posting_buffer;

  // Now we write out the posting list
  for (size_t i = 0; i < HASHMAP_SIZE; i++)
  {
    PostingList* bucket = buckets[i];

    while (bucket)
    {
      size_t hash = bucket->hash;

      dictionary_buffer[2 + hash * 2 + 1] = postingsOffset;

      postingBufferHead = write_uint64(postingBufferHead, bucket->length);
      // TODO: Write total collection frequency
      postingBufferHead = write_uint64(postingBufferHead, 0xdeadbeef);

      size_t lastPosition = 0;

      // TODO: Write index synchronization points and skip pointers

      for (uint32_t position : bucket->positions)
      {
        postingBufferHead = write_var_uint32(postingBufferHead, position - lastPosition);
        lastPosition = position;
      }

      postingsOffset += postingBufferHead - posting_buffer;

      write(fd, posting_buffer, postingBufferHead - posting_buffer);

      postingBufferHead = posting_buffer;

      bucket = bucket->nextBucket;
    }
  }

  lseek(fd, 0, SEEK_SET);
  write(fd, dictionary_buffer, needed * sizeof(uint64_t));
}

uint32_t Index::beginDocument(const char* url)
{
  documentNames.pushBack(url);
  return documentNames.size() - 1;
}

// TODO: ingest should only take the word and get the `doc_id` from the
// `Index`.
void Index::ingest(uint32_t docId, char const* word)
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

int main(int argc, char** argv)
{
  size_t length;
  char* content = memoryMapFile(argv[1], length);

  printf("Done mapping file!\n");

  avengine::vector<char*> documents;
  avengine::vector<char*> documentNames;
  parseKjvVerses(content, documents, documentNames);

  printf("Done parsing verses file, %ld documents!\n", documents.size());

  clock_t start = clock(), diff;
  for (size_t i = 0; i < documents.size(); i++)
  {
    indexDocument(documentNames[i], documents[i]);
  }

  diff = clock() - start;

  int msec = diff * 1000 / CLOCKS_PER_SEC;
  printf("Done building index in %d seconds %d milliseconds\n", msec / 1000, msec % 1000);

  start = clock();

  int fd = open("index.sfi", O_WRONLY | O_CREAT, 0644);
  globalIndex.serialize(fd);
  close(fd);

  diff = clock() - start;

  msec = diff * 1000 / CLOCKS_PER_SEC;
  printf("Done writing index in %d seconds %d milliseconds\n", msec / 1000, msec % 1000);
}
