#include "Utf8.h"

size_t SizeOfUtf8(Unicode c) {
  // The character can be represented the lower 7 bits.
  if (c <= 0x7f)
    return 1;

  if (c <= 0x7FF)
    return 2;

  if (c <= 0xFFFF)
    return 3;

  if (c <= 0x1FFFFF)
    return 4;

  if (c <= 0x3FFFFFF)
    return 5;

  return 6;
}

size_t SizeOfUtf16(Unicode c) {
  // The character can be represented as a single UTF-16 codepoint (2 bytes).
  if (c <= 0xffff)
    return 2;

  return 4;
}
