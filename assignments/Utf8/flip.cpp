#include "Utf8.h"
#include <iostream>
#include <fstream>
#include <sstream>
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
      std::cout.write(buffer, (head - ptr) * sizeof(Utf16));
      ptr = head;
    }
  }

  std::cout.write(buffer, (head - ptr) * sizeof(Utf16));
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

void convertUtf16toUtf8(char* content, size_t length, bool is_big_endian) {
    // If the BOM indicates you’re reading big-endian
    // Utf16, your flip utility should flip the byte sex of the data before decoding.
    const size_t byte_len = length;
    size_t char_loc = 0;
    bool skipped_bom = false;
    while (char_loc + 1 < byte_len) {
        const unsigned char b0 = static_cast<unsigned char>(content[char_loc]);
        const unsigned char b1 = static_cast<unsigned char>(content[char_loc + 1]);
        Utf16 units[2];
        if (is_big_endian) { // flipping byte sex
            units[0] = static_cast<Utf16>((b0 << 8) | b1);
        } else {
            units[0] = static_cast<Utf16>((b1 << 8) | b0);
        }
        size_t unit_count = 1;
        if (char_loc + 3 < byte_len) { // checking OOB
            const unsigned char b2 = static_cast<unsigned char>(content[char_loc + 2]);
            const unsigned char b3 = static_cast<unsigned char>(content[char_loc + 3]);
            if (is_big_endian) {
                units[1] = static_cast<Utf16>((b2 << 8) | b3);
            } else {
                units[1] = static_cast<Utf16>((b3 << 8) | b2);
            }
            unit_count = 2;
        }
        const Utf16* p = units;
        const Utf16* bound = units + unit_count;
        Unicode c = ReadUtf16(&p, bound);
        if (!skipped_bom && c == ByteOrderMark) { // avoiding converting BOM
            skipped_bom = true;
            char_loc += static_cast<size_t>(p - units) * 2;
            continue;
        }
        if (c == '\r') { // drop CR only when part of CRLF
            size_t next_loc = char_loc + static_cast<size_t>(p - units) * 2;
            if (next_loc + 1 < byte_len) {
                const unsigned char nb0 = static_cast<unsigned char>(content[next_loc]);
                const unsigned char nb1 = static_cast<unsigned char>(content[next_loc + 1]);
                Utf16 next_unit = is_big_endian ? static_cast<Utf16>((nb0 << 8) | nb1) : static_cast<Utf16>((nb1 << 8) | nb0);
                if (next_unit == '\n') {
                    char_loc = next_loc;
                    continue;
                }
            }
        }
        Utf8 buf[6];
        Utf8* end = WriteUtf8(buf, c, buf + 6);
        std::cout.write(reinterpret_cast<const char*>(buf), end - buf);
        char_loc += static_cast<size_t>(p - units) * 2;
    }
}


int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: flip <filename>\nRecognize the input file as either" <<
        " Utf8 or Utf16, convert\nit to the opposite encoding, and write the result to stdout.\n";
        return 1;
    }
    std::ifstream inputFile(argv[1], std::ios::binary);
    if (!inputFile.is_open()) {
        std::cerr << argv[1] << " not found." << std::endl;
        return 1;
    }

    size_t length;
    char *content = memory_map_file(argv[1], length);

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

    // std::cout << "File Contents:\n" << content << std::endl;
    // std::cout << "is_utf16:\n" << is_utf16 << std::endl;
    // std::cout << "is_big_endian:\n" << is_big_endian << std::endl;
    if (!is_utf16) {
        convertUtf8toUtf16(content, length); // When writing Utf16, use little-endian format.
    } else {
        convertUtf16toUtf8(content, length, is_big_endian);
    }
}
