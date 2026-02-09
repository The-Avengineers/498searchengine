#include "index.h"
#include <cassert>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
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
  close(fd);

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

PostingList::PostingList(char const* word, PostingList* nextBucket) : nextBucket(nextBucket)
{
  this->word = new char[strlen(word) + 1]();
  strcpy(this->word, word);
}

void PostingList::ingest(uint32_t docId, size_t offset)
{
  if (postings.empty() || postings.back().docId != docId)
  {
    postings.pushBack({.docId = docId, .termFreq = 0, .positions = positions.size() - 1});
  }

  Posting& posting = postings.back();

  posting.termFreq++;
  positions.pushBack(offset);
}

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

PostingList* Index::getBucket(char const* word)
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

  return bucket;
}

Index::Index() : buckets(HASHMAP_SIZE)
{
  for (PostingList*& bucket : buckets)
  {
    bucket = nullptr;
  }
}

void Index::serialize(int fd)
{
  size_t needed = 2;

  needed += (totalUniqWords + 1) * 2 * 2;

  uint64_t* dictionaryBuffer = new uint64_t[needed]();
  // std::fill(dictionary_buffer, dictionary_buffer + needed, UINT64_MAX);

  dictionaryBuffer[0] = documentNames.size();
  dictionaryBuffer[1] = totalUniqWords + 1;

  avengine::vector<char> stringBuffer;

  for (size_t i = 0; i < HASHMAP_SIZE; i++)
  {
    PostingList* bucket = buckets[i];

    while (bucket)
    {
      size_t hash = djb2Hash(bucket->word) % (totalUniqWords * 2);

      // Find an empty bucket
      while (dictionaryBuffer[2 + hash * 2] > 0)
      {
        hash += 1;
        hash %= (totalUniqWords * 2);
      }

      dictionaryBuffer[2 + hash * 2] = stringBuffer.size();

      // Add the term to the string buffer
      for (char const* str = bucket->word; *str; str++)
        stringBuffer.pushBack(*str);

      stringBuffer.pushBack('\0');

      bucket->hash = hash;
      bucket = bucket->nextBucket;
    }
  }

  // TODO: maybe fallocate?
  lseek(fd, needed * sizeof(uint64_t), SEEK_SET);

  write(fd, stringBuffer.begin(), stringBuffer.size());

  size_t postingsOffset = 0;

  // Allocate a buffer big buffer for writing, this should be changed to just what we need.
  uint8_t* postingBuffer = new uint8_t[16777216]();
  uint8_t* postingBufferHead = postingBuffer;

  // Now we write out the posting list
  for (size_t i = 0; i < HASHMAP_SIZE; i++)
  {
    PostingList* bucket = buckets[i];

    while (bucket)
    {
      size_t hash = bucket->hash;

      dictionaryBuffer[2 + hash * 2 + 1] = postingsOffset;

      postingBufferHead = write_uint64(postingBufferHead, bucket->postings.size());
      // TODO: Write total collection frequency
      postingBufferHead = write_uint64(postingBufferHead, 0xdeadbeef);

      size_t lastPosition = 0;

      // TODO: Write index synchronization points and skip pointers

      for (uint32_t position : bucket->positions)
      {
        postingBufferHead = write_var_uint32(postingBufferHead, position - lastPosition);
        lastPosition = position;
      }

      postingsOffset += postingBufferHead - postingBuffer;

      write(fd, postingBuffer, postingBufferHead - postingBuffer);

      postingBufferHead = postingBuffer;

      bucket = bucket->nextBucket;
    }
  }

  lseek(fd, 0, SEEK_SET);
  write(fd, dictionaryBuffer, needed * sizeof(uint64_t));
}

uint32_t Index::beginDocument(const char* url)
{
  documentNames.pushBack(url);
  currentDocId = static_cast<uint32_t>(documentNames.size() - 1);
  return currentDocId;
}

void Index::ingest(char const* word)
{
  assert(currentDocId != UINT32_MAX);
  PostingList* bucket = getBucket(word);

  bucket->ingest(currentDocId, offset++);
}

Index globalIndex;

size_t indexDocument(char const* url, char* pharses)
{
  globalIndex.beginDocument(url);
  size_t words = 0;

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

    globalIndex.ingest(word);
    words++;

    while (*pharses && !isalpha(*pharses))
      pharses++;
  }

  return words;
}

bool isDirectory(char const* path)
{
  struct stat s;
  assert(!stat(path, &s));
  return s.st_mode & S_IFDIR;
}

void listFiles(char* dir, avengine::vector<char*>& files)
{
  DIR* dr;
  struct dirent* en;
  dr = opendir(dir);
  if (dr)
  {
    while ((en = readdir(dr)) != NULL)
    {
      if (en->d_name[0] != '.')
      {
        char* nameCopy = new char[strlen(en->d_name) + 1];
        strcpy(nameCopy, en->d_name);
        files.pushBack(nameCopy);
      }
    }
    closedir(dr);
  }
}

int main(int argc, char** argv)
{
  clock_t start, diff;

  if (isDirectory(argv[1]))
  {
    avengine::vector<char*> files;

    listFiles(argv[1], files);

    start = clock();

    for (char* file : files)
    {
      char filepath[256];
      snprintf(filepath, sizeof(filepath), "%s%s", argv[1], file);

      size_t length;
      char* content = memoryMapFile(filepath, length);
      assert(content);

      size_t written = indexDocument(file, content);

      munmap(content, length);
    }
  }
  else
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
