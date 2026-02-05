#include "avengine/vector.h"
#include "index.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>

void parse_kjv_verses(char *content, avengine::vector<char *> &documents,
                      avengine::vector<char *> &document_names) {
  char *verse = strtok(content, "\n");

  while (verse) {
    document_names.pushBack(verse);

    while (*verse != '\t')
      verse++;

    *(verse++) = '\0';

    for (size_t i = 0; verse[i]; i++)
      if (verse[i] == ' ')
        verse[i] = '\0';

    documents.pushBack(verse);

    verse = strtok(nullptr, "\n");
  }
}

char *memory_map_file(char const *filename, size_t &length) {
  int fd = open(filename, O_RDONLY);
  struct stat sb;
  if (fd == -1)
    return nullptr;

  assert(fstat(fd, &sb) != -1);

  char *data = static_cast<char *>(
      mmap(NULL, sb.st_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0));

  length = sb.st_size;

  // This might cause undefined behavior if it hits a new page.
  data[length] = '\0';

  return data;
}

size_t djb2_hash(char const *str) {
  size_t hash = 5381;
  int c;

  while ((c = *str++))
    hash = ((hash << 5) + hash) + c; /* hash * 33 + c */

  return hash;
}

struct Posting {
  uint32_t doc_id;
  uint32_t term_freq;
  // It might make sense to make the just an index if resizing is often changing
  // the base pointer location.
  uint32_t *positions;
};

struct PostingList {
  PostingList(char const *word, PostingList *next_bucket) {
    this->word = word;
    this->next_bucket = next_bucket;
    capacity = 4096;
    length = 0;

    postings = (Posting *)malloc(this->capacity * sizeof(Posting));
    positions_start = position_head = (uint32_t *)malloc(8 * sizeof(uint32_t));
    positions_end = &positions_start[8];
  }

  void ingest(uint32_t doc_id, size_t offset) {
    if (!length || postings[length - 1].doc_id != doc_id) {
      if (length >= capacity) {
        capacity *= 2;
        postings =
            (Posting *)realloc((void *)postings, capacity * sizeof(Posting));
      }

      postings[length].doc_id = doc_id;
      postings[length].positions = position_head;
      postings[length++].term_freq = 0;
    }

    Posting &posting = postings[length - 1];

    posting.term_freq++;

    if (position_head >= positions_end) {
      size_t current_capacity = positions_end - positions_start;
      size_t new_capacity = current_capacity * 2;

      size_t position_head_offset = position_head - positions_start;
      positions_start =
          (uint32_t *)realloc(positions_start, new_capacity * sizeof(uint32_t));

      positions_end = positions_start + new_capacity;
      position_head = positions_start + position_head_offset;

      // We have to update the pointers the postings. In future implentions this
      // may not be necessary.
      for (size_t i = 0; i < length; i++) {
        size_t offset =
            postings[i].positions - (positions_start - position_head_offset);
        postings[i].positions = positions_start + offset;
      }
    }

    *(position_head++) = offset;
  }

  char const *word;
  PostingList *next_bucket;
  size_t capacity;
  size_t length;
  Posting *postings;

  uint32_t *positions_end;
  uint32_t *position_head;
  uint32_t *positions_start;
};

struct Index {
  PostingList **buckets;
  size_t offset = 0;
  size_t total_uniq_words = 0;
  avengine::vector<const char *> document_names;

  PostingList *&get_bucket(char const *word) {
    size_t index = djb2_hash(word) % 4096;
    PostingList *&bucket = buckets[index];

    while (bucket && strcmp(bucket->word, word))
      bucket = bucket->next_bucket;

    if (!bucket)
      bucket = new PostingList(word, nullptr);

    return bucket;
  }

  Index() { buckets = new PostingList *[4096](); }

  uint32_t begin_document(const char *url) {
    document_names.pushBack(url);
    offset = 0;
    return document_names.size() - 1;
  }

  // TODO: ingest should only take the word and get the `doc_id` from the
  // `Index`.
  void ingest(uint32_t doc_id, char const *word) {
    size_t index = djb2_hash(word) % 4096;
    PostingList *bucket = buckets[index];

    while (bucket && strcmp(bucket->word, word))
      bucket = bucket->next_bucket;

    if (!bucket) {
      total_uniq_words++;
      bucket = buckets[index] = new PostingList(word, buckets[index]);
    }

    bucket->ingest(doc_id, offset++);
  }
};

Index global_index;

size_t index_document(char const *url, char *words) {
  uint32_t doc_id = global_index.begin_document(url);

  while (*words) {
    char const *word = words;

    global_index.ingest(doc_id, word);

    while (*words)
      words++;

    words++;
  }

  return 0;
}

void two_way_merge(const char *first_word, const char *second_word) {
  PostingList *first_bucket = global_index.get_bucket(first_word);
  PostingList *second_bucket = global_index.get_bucket(second_word);

  size_t first_index = 0;
  size_t second_index = 0;

  while (first_index < first_bucket->length &&
         second_index < second_bucket->length) {
    uint32_t first_id = first_bucket->postings[first_index].doc_id;
    uint32_t second_id = second_bucket->postings[second_index].doc_id;

    if (first_id > second_id) {
      second_index++;
    } else if (first_id < second_id) {
      first_index++;
    } else {
      printf("%s matches both %s and %s\n",
             global_index.document_names[first_id], first_word, second_word);
      first_index++;
      second_index++;
    }
  }
}

void inspect_bucket(PostingList *bucket) {
  for (size_t i = 0; i < bucket->length; i++) {
    printf("%s :", global_index.document_names[bucket->postings[i].doc_id]);

    for (size_t j = 0; j < bucket->postings[i].term_freq; j++)
      printf(" %d", bucket->postings[i].positions[j]);

    printf("\n");
  }
}

int main(int argc, char **argv) {
  size_t length;
  char *content = memory_map_file("../../data/kjv.txt", length);

  avengine::vector<char *> documents;
  avengine::vector<char *> document_names;
  parse_kjv_verses(content, documents, document_names);

  for (size_t i = 0; i < documents.size(); i++)
    index_document(document_names[i], documents[i]);

  two_way_merge(argv[1], argv[2]);
}
