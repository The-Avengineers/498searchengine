#include "Utf8.h"
#include <iostream>
#include <cassert>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>

void convertUtf8toUtf16(const char *content, size_t length) {
  // When writing Utf16, use little-endian format.
  const char bom_le[2] = {static_cast<char>(0xff), static_cast<char>(0xfe)};
  std::cout.write(bom_le, 2);

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
    ptr = WriteUtf16(ptr, c);

    if (ptr >= end) {
      std::cout.write(buffer, (ptr - head) * sizeof(Utf16));
      ptr = head;
    }
  }

  std::cout.write(buffer, (ptr - head) * sizeof(Utf16));
}

char *memory_map_file(char *filename, size_t &length) {
  int fd = open(filename, O_RDONLY);
  struct stat sb;
  if(fd == -1)
      return nullptr;

  assert(fstat(fd, &sb) != -1);

  char *data = static_cast<char *>(
      mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0));

  length = sb.st_size;

  return data;
}

void convertUtf16toUtf8(char* content, size_t length, bool is_big_endian) {
  // If the BOM indicates you’re reading big-endian
  // Utf16, your flip utility should flip the byte sex of the data before
  // decoding.
  const Utf16 *p = reinterpret_cast<const Utf16 *>(content);
  const Utf16 *bound = reinterpret_cast<const Utf16 *>(content + length);

  char buffer[1024] = {};
  std::fill_n(buffer, 1024, 0xff);
  Utf8 *head = reinterpret_cast<Utf8 *>(buffer);
  Utf8 *ptr = reinterpret_cast<Utf8 *>(buffer);
  Utf8 *end = reinterpret_cast<Utf8 *>(&buffer[1024 - 6]);

  while (p < bound) {
    Unicode c;

    if(is_big_endian) {
      Utf16 mini_buffer[2];
      // If the content is not a multiple of two this will cause undefined
      // behavior, but that should never happen.
      mini_buffer[0] = __builtin_bswap16(p[0]);
      mini_buffer[1] = __builtin_bswap16(p[1]);
      Utf16 const *mini_buffer_head = mini_buffer;

      c = ReadUtf16(&mini_buffer_head);

      p += mini_buffer_head - mini_buffer;
    } else
      c = ReadUtf16(&p);

    ptr = WriteUtf8(ptr, c);

    if (ptr >= end) {
      std::cout.write(buffer, (ptr - head) * sizeof(Utf8));
      ptr = head;
    }
  }

  // Don't ask me why was need to subtract one here and not anywhere else.
  std::cout.write(buffer, (ptr - head - 1) * sizeof(Utf8));
}


int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: flip <filename>\nRecognize the input file as either" <<
        " Utf8 or Utf16, convert\nit to the opposite encoding, and write the result to stdout.\n";
        return 1;
    }

    size_t length;
    char *content = memory_map_file(argv[1], length);

    if(!content) {
      std::cerr << argv[1] << " not found." << std::endl;
      return 1;
    }

    bool is_utf16 = false;
    bool is_big_endian = false;

    if (length >= 2) {
        const unsigned char b0 = static_cast<unsigned char>(content[0]);
        const unsigned char b1 = static_cast<unsigned char>(content[1]);
        if (b0 == 0xff && b1 == 0xfe) {
            is_utf16 = true;
            is_big_endian = false;
        } else if (b0 == 0xfe && b1 == 0xff) {
            is_utf16 = true;
            is_big_endian = true;
        } else if (length >= 3 &&
                    b0 == Utf8BOMString[0] &&
                    b1 == Utf8BOMString[1] &&
                    static_cast<unsigned char>(content[2]) == Utf8BOMString[2]) {
            is_utf16 = false;
        }
    }

    if (!is_utf16) {
        convertUtf8toUtf16(content, length); // When writing Utf16, use little-endian format.
    } else {
        convertUtf16toUtf8(content + 2, length, is_big_endian);
    }
}
