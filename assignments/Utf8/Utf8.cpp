#include "Utf8.h"
#include <cassert>
#include <iostream>

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
  if (c > 0x10ffff)
    return 2;
  return 4;
}

bool IsValidUtf8First(const Utf8* p) {
  return *p == 0xFE || *p == 0xFF || (*p <= 0b01111111)
  || (*p & 0xE0) == 0xC0
  || (*p & 0xF0) == 0xE0
  || (*p & 0xF8) == 0xF0
  || (*p & 0xFC) == 0xF8
  || (*p & 0xFE) == 0xFC;
}

size_t inline IndicatedLength( const Utf8 *p ) {
  // checking if the first byte matches any one of the cases
  if (*p <= 0b01111111) {
    return 1;
  } else if ((*p & 0xE0) == 0xC0) {
    return 2;
  } else if ((*p & 0xF0) == 0xE0) {
    return 3;
  } else if ((*p & 0xF8) == 0xF0) {
    return 4;
  } else if ((*p & 0xFC) == 0xF8) {
    return 5;
  } else if ((*p & 0xFE) == 0xFC) {
    return 6;
  } else { // Invalid first byte
    return 1;
  }
}

#define PARSE_UTF8(OFFSET) { \
  if (bound && *p >= bound) \
    return ReplacementCharacter; \
  \
  if((**p & 0xC0) != 0x80) \
    return ReplacementCharacter; \
  \
  c |= static_cast<Unicode>(*(*p)++ & 0x3F) << OFFSET * 6; \
}

Unicode ReadUtf8( const Utf8 **p, const Utf8 *bound) {
  Unicode c = ReplacementCharacter;

  if (bound && *p >= bound)
    return 0;

  if(**p <= 0x7F)
    return *(*p)++;

  size_t length = IndicatedLength(*p);

  if (length == 2) {
    c = static_cast<Unicode>(*(*p)++ & 0x1F) << 6;
    PARSE_UTF8(0);

    if(c <= 0x7f)
      return ReplacementCharacter;
  } else if (length == 3) {
    c = static_cast<Unicode>(*(*p)++ & 0xF) << 12;
    PARSE_UTF8(1);
    PARSE_UTF8(0);

    if (c <= 0x7ff)
      return ReplacementCharacter;
  } else if (length == 4) {
    c = static_cast<Unicode>(*(*p)++ & 0x7) << 18;
    PARSE_UTF8(2);
    PARSE_UTF8(1);
    PARSE_UTF8(0);

    if (c <= 0xffff)
      return ReplacementCharacter;
  } else if (length == 5) {
    c = static_cast<Unicode>(*(*p)++ & 0x3) << 24;
    PARSE_UTF8(3);
    PARSE_UTF8(2);
    PARSE_UTF8(1);
    PARSE_UTF8(0);

    if (c <= 0x1fffff)
      return ReplacementCharacter;
  } else if (length == 6) {
    c = static_cast<Unicode>(*(*p)++ & 0x1) << 30;
    PARSE_UTF8(4);
    PARSE_UTF8(3);
    PARSE_UTF8(2);
    PARSE_UTF8(1);
    PARSE_UTF8(0);

    if (c <= 0x3ffffff)
      return ReplacementCharacter;
  } else {
    (*p)++;
  }

  if (c >= 0xd800 && c <= 0xdfff || // UTF-16 surrogates
      c >= 0xfdd0 && c <= 0xfdef || // noncharacters
      c == 0xfffe || c == 0xffff)   // special values
    c = ReplacementCharacter;

  return c;
}

const Utf8 *PreviousUtf8( const Utf8 *p, const Utf8 *bound) {
  do
    p--;
  while((!bound || p >= bound) && !IsValidUtf8First(p));

  return p;
}

Utf8 *WriteUtf8( Utf8 *p, Unicode c, Utf8 *bound) {
  if (c > 0x7fffffff)
    c = ReplacementCharacter;

  // Valid UTF-8 Sequences:
  //
  //   U-00000000 - U-0000007F:   0xxxxxxx 7 bits
  //   U-00000080 - U-000007FF:   110xxxxx 10xxxxxx 11 bits
  //   U-00000800 - U-0000FFFF:   1110xxxx 10xxxxxx 10xxxxxx 16 bits
  //   U-00010000 - U-001FFFFF:   11110xxx 10xxxxxx 10xxxxxx 10xxxxxx 21 bits
  //   U-00200000 - U-03FFFFFF:   111110xx 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx 26 bits
  //   U-04000000 - U-7FFFFFFF:   1111110x 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx 31 bits
  //
  if (c <= 0x7f && (!bound || p < bound)) {
    *p++ = static_cast<Utf8>(c);
  } else if (c <= 0x7FF && (!bound || p + 1 < bound)) {

    *p++ = static_cast<Utf8>(0xc0 | ((c >> 6) & 0x1f));
    *p++ = static_cast<Utf8>(0x80 | (c & 0x3f));
  } else if (c <= 0xFFFF && (!bound || p + 2 < bound)) {
    *p++ = static_cast<Utf8>(0xe0 | ((c >> 12) & 0x0f));
    *p++ = static_cast<Utf8>(0x80 | ((c >> 6) & 0x3f));
    *p++ = static_cast<Utf8>(0x80 | (c & 0x3f));
  } else if (c <= 0x1FFFFF && (!bound || p + 3 < bound)) {
    *p++ = static_cast<Utf8>(0xf0 | ((c >> 18) & 0x07));
    *p++ = static_cast<Utf8>(0x80 | ((c >> 12) & 0x3f));
    *p++ = static_cast<Utf8>(0x80 | ((c >> 6) & 0x3f));
    *p++ = static_cast<Utf8>(0x80 | (c & 0x3f));
  } else if (c <= 0x3FFFFFF && (!bound || p + 4 < bound)) {
    *p++ = static_cast<Utf8>(0xf8 | ((c >> 24) & 0x03));
    *p++ = static_cast<Utf8>(0x80 | ((c >> 18) & 0x3f));
    *p++ = static_cast<Utf8>(0x80 | ((c >> 12) & 0x3f));
    *p++ = static_cast<Utf8>(0x80 | ((c >> 6) & 0x3f));
    *p++ = static_cast<Utf8>(0x80 | (c & 0x3f));
  } else if((!bound || p + 5 < bound)) {
    *p++ = static_cast<Utf8>(0xfc | ((c >> 30) & 0x01));
    *p++ = static_cast<Utf8>(0x80 | ((c >> 24) & 0x3f));
    *p++ = static_cast<Utf8>(0x80 | ((c >> 18) & 0x3f));
    *p++ = static_cast<Utf8>(0x80 | ((c >> 12) & 0x3f));
    *p++ = static_cast<Utf8>(0x80 | ((c >> 6) & 0x3f));
    *p++ = static_cast<Utf8>(0x80 | (c & 0x3f));
  }
  return p;
}


// Same functionality as ReadUtf8, w/o advancing p. Consider merging them into one func?
Unicode GetUtf8( const Utf8 *p, const Utf8 *bound) {
  return ReadUtf8(&p, bound);
}

const Utf8 *NextUtf8( const Utf8 *p, const Utf8 *bound) {
  ReadUtf8(&p, bound);
  return p;
}

Utf16 *WriteUtf16(Utf16 *p, Unicode c, Utf16 *bound) {
  // If we don't have room in the pointer just return.
  if (bound && p >= bound)
    return p;

  if (c > 0x10ffff) {
    *p = ReplacementCharacter;
    return p + 1;
  }

  if (c <= 0xffff) {
    *p = c;
    return p + 1;
  }

  c -= 0x10000;

  // All encoding is little-endian, so write low-byte first.
  *(p++) = 0xd800 + ((c >> 10) & 0x3ff);

  if (bound && p >= bound)
    return p;

  *(p++) = 0xdc00 + (c & 0x3ff);

  return p;
}

Unicode ReadUtf16(const Utf16 **p, const Utf16 *bound) {
  if (bound && *p >= bound)
    return 0;

  // Unpaired or out-of-order surrogates are read as literal values.
  Utf16 first = **p;
  if (first < 0xd800 || first > 0xdbff) {
    return *((*p)++);
  }
  if (bound && (*p + 1) >= bound) {
    return *((*p)++);
  }
  Utf16 second = (*p)[1];
  if (second < 0xdc00 || second > 0xdfff) {
    return *((*p)++);
  }

  (*p) += 2;
  int high_surrogate = (first - 0xd800) << 10;
  int low_surrogate = (second - 0xdc00);
  return 0x10000 + (high_surrogate | low_surrogate);
}

Unicode GetUtf16(const Utf16 *p, const Utf16 *bound) {
  return ReadUtf16(&p, bound);
}

const Utf16 *NextUtf16(const Utf16 *p, const Utf16 *bound) {
  auto pp = &p;
  ReadUtf16(pp, bound);
  return *pp;
}
