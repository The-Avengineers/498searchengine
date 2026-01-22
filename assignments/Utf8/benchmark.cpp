#include "Utf8.h"
#include <cassert>
#include <fcntl.h>
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>

template <typename T> void black_box(T &&d) {
  asm volatile("" : "+g"(d) : : "memory");
}

void convertUtf8toUtf16(const char *content, size_t length) {
  char buffer[1024] = {};
  Utf16 *head = reinterpret_cast<Utf16 *>(buffer);
  Utf16 *ptr = reinterpret_cast<Utf16 *>(buffer);
  Utf16 *end = reinterpret_cast<Utf16 *>(&buffer[1022]);

  const Utf8 *p = reinterpret_cast<const Utf8 *>(content);
  const Utf8 *bound = p + length;

  if (length >= 3 && p[0] == Utf8BOMString[0] && p[1] == Utf8BOMString[1] &&
      p[2] == Utf8BOMString[2]) {
    p += 3;
  }

  while (p < bound) {
    Unicode c = ReadUtf8(&p, bound);
    WriteUtf16(ptr, c);
    black_box(ptr);
  }
}

char *memory_map_file(char *filename, size_t &length) {
  int fd = open(filename, O_RDONLY);
  struct stat sb;
  assert(fd != -1);
  assert(fstat(fd, &sb) != -1);

  char *data = static_cast<char *>(
      mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0));

  length = sb.st_size;

  assert(data != MAP_FAILED);

  return data;
}

int main(int argc, char *argv[]) {
  if (argc != 2) {
    std::cerr << "Usage: flip <filename>\nRecognize the input file as either"
              << " Utf8 or Utf16, convert\nit to the opposite encoding, and "
                 "write the result to stdout.\n";
    return 1;
  }

  size_t length;
  char *content = memory_map_file(argv[1], length);

  for (size_t i = 0; i < 20; i++) {
    convertUtf8toUtf16(content, length);
  }
}
