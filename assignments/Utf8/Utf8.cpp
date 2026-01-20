#include "Utf8.h"
#include <cassert>

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

bool IsValidUtf8First(const Utf8* p) {
  return (*p >= 0b00000000 && *p <= 0b01111111) ||
         (*p >= 0b11000000 && *p <= 0b11011111) ||
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
  Utf8 buf[len];
  // 1.  If *pp already points to or past the last valid byte, do not
  //     advance *pp and return the null character.
  if (!IsValidUtf8First(*p) || (bound != nullptr && *p >= bound)) {
    return 0; // Null character
  }
  for (size_t i = 0; i < len; i++) {
    if (bound != nullptr && *p >= bound) { // *p runs OOB
      return ReplacementCharacter;
    }
    if ((i >= 1) && (**p < 0b10000000 || **p > 0b10111111)) {
      return ReplacementCharacter;
    }
    buf[i] = **p;
    *p++;
  }
  if (len >= 5) { // overlong characters
    return ReplacementCharacter;
  }
  Unicode* result = reinterpret_cast<Unicode*>(result);
  if (*result >= 0xd800 && *result <= 0xdfff || // UTF-16 surrogates
      *result >= 0xfdd0 && *result <= 0xfdef || // noncharacters
      *result == 0xfffe && *result == 0xffff // special values
    ) {
    return ReplacementCharacter;
  }
  return *result;
}

const Utf8 *PreviousUtf8( const Utf8 *p, const Utf8 *bound) {
  Utf8* ptr = const_cast<Utf8*>(p);
  if (bound == nullptr) {
    while (true) {
      if (IsValidUtf8First(ptr)) {
        return ptr;
      }
      ptr--;
    }
  } else if (bound != nullptr) {
    assert(IsValidUtf8First(p));
    while (ptr >= bound) {
      if (IsValidUtf8First(ptr)) {
        return ptr;
      }
      ptr--;
    }
  }
}

Utf8 *WriteUtf8( Utf8 *p, Unicode c, Utf8 *bound) {
  Unicode char_to_write = c;
  if (c > 0x7fffffff) {
    c = ReplacementCharacter;
  }
  if (bound != nullptr && p >= bound) { 
    //If p is already at or past the bound, 
    // do nothing and return p.
    return p;
  }
  // write to the last possible byte until pass the bound
  Utf8 buf[4];

  buf[0] = (c & 0x000000ff);
  buf[1] = (c & 0x0000ff00) >> 8;
  buf[2] = (c & 0x00ff0000) >> 16;
  buf[3] = (c & 0xff000000) >> 24;
  // TODO: Check if there is off-by-one
  for (size_t i = 0; i < 4; i++) {
    if (bound != nullptr && p + i + 1 >= bound) { 
    //If p is already at or past the bound, 
    // do nothing and return p.
      return p + i;
    }
    *(p + i) = buf[i];
  }
  return p + 4;
}


// Same functionality as ReadUtf8, w/o advancing p. Consider merging them into one func?
Unicode GetUtf8( const Utf8 *p, const Utf8 *bound) {
  size_t len = IndicatedLength(p);
  Utf8 buf[len];
  // 1.  If *pp already points to or past the last valid byte, do not
  //     advance *pp and return the null character.
  if (!IsValidUtf8First(p) || (bound != nullptr && p >= bound)) {
    return 0; // Null character
  }
  for (size_t i = 0; i < len; i++) {
    if (bound != nullptr && p >= bound) { // *p runs OOB
      return ReplacementCharacter;
    }
    if ((i >= 1) && (*p < 0b10000000 || *p > 0b10111111)) {
      return ReplacementCharacter;
    }
    buf[i] = *p;
  }
  if (len >= 5) { // overlong characters
    return ReplacementCharacter;
  }
  Unicode* result = reinterpret_cast<Unicode*>(result);
  if (*result >= 0xd800 && *result <= 0xdfff || // UTF-16 surrogates
      *result >= 0xfdd0 && *result <= 0xfdef || // noncharacters
      *result == 0xfffe && *result == 0xffff // special values
    ) {
    return ReplacementCharacter;
  }
  return *result;
}

// TODO: Check OOB behavior
const Utf8 *NextUtf8( const Utf8 *p, const Utf8 *bound) {
  if (bound != nullptr && p + 1 >= bound) {
    return p;
  }
  return p + 1;
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

  size_t expected_size;

  // If we try to read a invalid character, just read it as a raw number. We
  // should also handle if we have too read two words, but the buffer ends.
  if (*p[0] <= 0xd800 || *p[0] >= 0xdbff || (*p)[1] <= 0xdc00)
    expected_size = 1;
  else
    expected_size = 2;

  if (bound && *p + expected_size > bound)
    return 0;

  if (expected_size == 1) {
    return *((*p)++);
  }

  int high_surrogate = (*((*p)++) - 0xd800) << 10;
  int low_surrogate = (*((*p)++) - 0xdc00);

  return high_surrogate | low_surrogate;
}

Unicode GetUtf16(const Utf16 *p, const Utf16 *bound) {
  return ReadUtf16(&p, bound);
}

const Utf16 *NextUtf16(const Utf16 *p, const Utf16 *bound) {
  auto pp = &p;
  ReadUtf16(pp);
  return *pp;
}
