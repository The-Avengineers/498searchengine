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
  return (*p >= 0b00000000 && *p <= 0b01111111) ||
         (*p >= 0b11000000 && *p <= 0b11011111) ||
         (*p >= 0b11100000 && *p <= 0b11101111) ||
         (*p >= 0b11110000 && *p <= 0b11110111) ||
         (*p >= 0b11111000 && *p <= 0b11111011) || 
         (*p >= 0b11111100 && *p <= 0b11111101);
}

size_t IndicatedLength( const Utf8 *p ) {
  // checking if the first byte matches any one of the cases
  if (*p >= 0b00000000 && *p <= 0b01111111) {
    return 1;
  } else if (*p >= 0b11000000 && *p <= 0b11011111) {
    return 2;
  } else if (*p >= 0b11100000 && *p <= 0b11101111) {
    return 3;
  } else if (*p >= 0b11110000 && *p <= 0b11110111) {
    return 4;
  } else if (*p >= 0b11111000 && *p <= 0b11111011) {
    return 5;
  } else if (*p >= 0b11111100 && *p <= 0b11111101) {
    return 6;
  } else { // Invalid first byte
    return 1;
  }
}

Unicode ReadUtf8( const Utf8 **p, const Utf8 *bound) {
  size_t len = IndicatedLength(*p);
  // std::cout << "len: " << len << '\n';
  Utf8 buf[len];
  // 1.  If *pp already points to or past the last valid byte, do not
  //     advance *pp and return the null character.
  if (bound != nullptr && *p >= bound) {
    return 0; // Null character
  }
  if (!IsValidUtf8First(*p)) {
    (*p)++;
    return ReplacementCharacter;
  }
  const Utf8 *q = *p; // allocating another cursor
  for (size_t i = 0; i < len; i++) {
    if (bound != nullptr && q >= bound) { // ran out of bytes
      *p = q;
      return ReplacementCharacter;
    }
    buf[i] = *q;
    if (i >= 1 && (buf[i] < 0b10000000 || buf[i] > 0b10111111)) {
      *p = q + 1;
      return ReplacementCharacter;
    }
    q++;
  }
  *p = q;
  Unicode result = 0;
  if (len == 1) {
    result = buf[0];
  } else if (len == 2) {
    result = ((buf[0] & 0x1f) << 6) |
              (buf[1] & 0x3f);
    if (result <= 0x7f)
      result = ReplacementCharacter;
  } else if (len == 3) {
    result = ((buf[0] & 0x0f) << 12) |
             ((buf[1] & 0x3f) << 6) |
             (buf[2] & 0x3f);
    if (result <= 0x7ff)
      result = ReplacementCharacter;
  } else if (len == 4) {
    result = ((buf[0] & 0x07) << 18) |
             ((buf[1] & 0x3f) << 12) |
             ((buf[2] & 0x3f) << 6) |
             (buf[3] & 0x3f);
    if (result <= 0xffff)
      result = ReplacementCharacter;
  } else if (len == 5) {
    result = ((buf[0] & 0x03) << 24) |
             ((buf[1] & 0x3f) << 18) |
             ((buf[2] & 0x3f) << 12) |
             ((buf[3] & 0x3f) << 6) |
             (buf[4] & 0x3f);
    if (result <= 0x1fffff)
      result = ReplacementCharacter;
  } else if (len == 6) {
    result = ((buf[0] & 0x01) << 30) |
             ((buf[1] & 0x3f) << 24) |
             ((buf[2] & 0x3f) << 18) |
             ((buf[3] & 0x3f) << 12) |
             ((buf[4] & 0x3f) << 6) |
             (buf[5] & 0x3f);
    if (result <= 0x3ffffff)
      result = ReplacementCharacter;
  } else {
    result = ReplacementCharacter;
  }

  if (result >= 0xd800 && result <= 0xdfff || // UTF-16 surrogates
      result >= 0xfdd0 && result <= 0xfdef || // noncharacters
      result == 0xfffe || result == 0xffff) { // special values
    result = ReplacementCharacter;
  }
  return result;
}

const Utf8 *PreviousUtf8( const Utf8 *p, const Utf8 *bound) {
  if (p == nullptr) {
    return nullptr;
  }
  const Utf8* ptr = p - 1; // start scanning before p
  if (bound == nullptr) {
    while (true) {
      if (IsValidUtf8First(ptr)) {
        return ptr;
      }
      ptr--;
    }
  } else {
    while (ptr >= bound) {
      if (IsValidUtf8First(ptr)) {
        return ptr;
      }
      ptr--;
    }
    return p;
  }
}

Utf8 *WriteUtf8( Utf8 *p, Unicode c, Utf8 *bound) {
  if (c > 0x7fffffff) {
    c = ReplacementCharacter;
  }
  if (bound != nullptr && p >= bound) { 
    //If p is already at or past the bound, 
    // do nothing and return p.
    return p;
  }
  size_t len = SizeOfUtf8(c);
  if (bound != nullptr && p + len > bound) {
    return p;
  }
  // Valid UTF-8 Sequences:
  //
  //   U-00000000 - U-0000007F:   0xxxxxxx 7 bits
  //   U-00000080 - U-000007FF:   110xxxxx 10xxxxxx 11 bits
  //   U-00000800 - U-0000FFFF:   1110xxxx 10xxxxxx 10xxxxxx 16 bits
  //   U-00010000 - U-001FFFFF:   11110xxx 10xxxxxx 10xxxxxx 10xxxxxx 21 bits
  //   U-00200000 - U-03FFFFFF:   111110xx 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx 26 bits
  //   U-04000000 - U-7FFFFFFF:   1111110x 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx 31 bits
  //
  if (len == 1) {
    *p++ = static_cast<Utf8>(c);
  } else if (len == 2) {
    *p++ = static_cast<Utf8>(0xc0 | ((c >> 6) & 0x1f));
    *p++ = static_cast<Utf8>(0x80 | (c & 0x3f));
  } else if (len == 3) {
    *p++ = static_cast<Utf8>(0xe0 | ((c >> 12) & 0x0f));
    *p++ = static_cast<Utf8>(0x80 | ((c >> 6) & 0x3f));
    *p++ = static_cast<Utf8>(0x80 | (c & 0x3f));
  } else if (len == 4) {
    *p++ = static_cast<Utf8>(0xf0 | ((c >> 18) & 0x07));
    *p++ = static_cast<Utf8>(0x80 | ((c >> 12) & 0x3f));
    *p++ = static_cast<Utf8>(0x80 | ((c >> 6) & 0x3f));
    *p++ = static_cast<Utf8>(0x80 | (c & 0x3f));
  } else if (len == 5) {
    *p++ = static_cast<Utf8>(0xf8 | ((c >> 24) & 0x03));
    *p++ = static_cast<Utf8>(0x80 | ((c >> 18) & 0x3f));
    *p++ = static_cast<Utf8>(0x80 | ((c >> 12) & 0x3f));
    *p++ = static_cast<Utf8>(0x80 | ((c >> 6) & 0x3f));
    *p++ = static_cast<Utf8>(0x80 | (c & 0x3f));
  } else {
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
  size_t len = IndicatedLength(p);
  // std::cout << "len: " << len << '\n';
  Utf8 buf[len];
  // 1.  If *pp already points to or past the last valid byte, do not
  //     advance *pp and return the null character.
  if (bound != nullptr && p >= bound) {
    return 0; // Null character
  }
  if (!IsValidUtf8First(p)) {
    return ReplacementCharacter;
  }
  const Utf8 *q = p; // allocating another cursor
  for (size_t i = 0; i < len; i++) {
    if (bound != nullptr && q >= bound) { // ran out of bytes
      return ReplacementCharacter;
    }
    buf[i] = *q;
    if (i >= 1 && (buf[i] < 0b10000000 || buf[i] > 0b10111111)) {
      return ReplacementCharacter;
    }
    q++;
  }

  Unicode result = 0;
  if (len == 1) {
    result = buf[0];
  } else if (len == 2) {
    result = ((buf[0] & 0x1f) << 6) |
              (buf[1] & 0x3f);
    if (result <= 0x7f)
      result = ReplacementCharacter;
  } else if (len == 3) {
    result = ((buf[0] & 0x0f) << 12) |
             ((buf[1] & 0x3f) << 6) |
             (buf[2] & 0x3f);
    if (result <= 0x7ff)
      result = ReplacementCharacter;
  } else if (len == 4) {
    result = ((buf[0] & 0x07) << 18) |
             ((buf[1] & 0x3f) << 12) |
             ((buf[2] & 0x3f) << 6) |
             (buf[3] & 0x3f);
    if (result <= 0xffff)
      result = ReplacementCharacter;
  } else if (len == 5) {
    result = ((buf[0] & 0x03) << 24) |
             ((buf[1] & 0x3f) << 18) |
             ((buf[2] & 0x3f) << 12) |
             ((buf[3] & 0x3f) << 6) |
             (buf[4] & 0x3f);
    if (result <= 0x1fffff)
      result = ReplacementCharacter;
  } else if (len == 6) {
    result = ((buf[0] & 0x01) << 30) |
             ((buf[1] & 0x3f) << 24) |
             ((buf[2] & 0x3f) << 18) |
             ((buf[3] & 0x3f) << 12) |
             ((buf[4] & 0x3f) << 6) |
             (buf[5] & 0x3f);
    if (result <= 0x3ffffff)
      result = ReplacementCharacter;
  } else {
    result = ReplacementCharacter;
  }

  if (result >= 0xd800 && result <= 0xdfff || // UTF-16 surrogates
      result >= 0xfdd0 && result <= 0xfdef || // noncharacters
      result == 0xfffe || result == 0xffff) { // special values
    result = ReplacementCharacter;
  }
  return result;
}

const Utf8 *NextUtf8( const Utf8 *p, const Utf8 *bound) {
  if (p == nullptr) {
    return nullptr;
  }
  const Utf8* q = p;
  Unicode c = ReadUtf8(&q, bound);
  if (q == nullptr) {
    return p;
  }
  return q;
}

Utf16 *WriteUtf16(Utf16 *p, Unicode c, Utf16 *bound) {
  if (c > 0x10ffff)
    c = ReplacementCharacter;

  size_t needed = SizeOfUtf16(c);

  // If we don't have room in the pointer just return.
  if (bound && p >= bound)
    return p;

  // This is the simple case. We just write the character to p.
  if (needed == 2)
    *(p++) = c;

  if (needed == 4) {
    c -= 0x10000;

    // All encoding is little-endian, so write low-byte first.
    *(p++) = 0xd800 + ((c >> 10) & 0x3ff);

    if (bound && p >= bound)
      return p;

    *(p++) = 0xdc00 + (c & 0x3ff);
  }

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
