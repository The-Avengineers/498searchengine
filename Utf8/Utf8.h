// Utf8.h
//
// Nicole Hamilton, nham@umich.edu

// This file defines some basic utility functions for reading and
// writing UTF-8 and UTF-16 characters, translating to/from Unicode.


#pragma once

#include <cstddef>
#include <cstdint>


//
// Basic Types
//


typedef uint32_t Unicode;
typedef uint8_t  Utf8;
typedef uint16_t Utf16;


//
// Unicode
//


// Unicode is a standard that defines a codespace of code points in the range of
// 0 to 1,114,111 (0x10ffff) (21 bits).
//
// Code points assign values to various glyphs (characters and symbols), including
// all the characters used in all the world's languages and almost 4,000 emojis. Code
// points 0x00 to 0x7f are the ASCII characters.
//
// Some code points are forbidden due to security risks or to reserve them for
// internal use.


//
// UTF-8 Format
//


// UTF-8 encodes Unicode character values into a varying length sequence of bytes.
// It uses the same one-byte encoding for ASCII character values 0x00 to 0x7f but
// more bytes for larger character values.
//
// Following http://www.cl.cam.ac.uk/~mgk25/unicode.html#utf-8

// Valid UTF-8 Sequences:
//
//   U-00000000 - U-0000007F:   0xxxxxxx 7 bits
//   U-00000080 - U-000007FF:   110xxxxx 10xxxxxx 11 bits
//   U-00000800 - U-0000FFFF:   1110xxxx 10xxxxxx 10xxxxxx 16 bits
//   U-00010000 - U-001FFFFF:   11110xxx 10xxxxxx 10xxxxxx 10xxxxxx 21 bits
//   U-00200000 - U-03FFFFFF:   111110xx 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx 26 bits
//   U-04000000 - U-7FFFFFFF:   1111110x 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx 31 bits
//
//
// Invalid, overlong UTF-8 Sequences that use more bytes than required
// should never be accepted because of the security risk.
//
//                              1100000x (10xxxxxx) 11 bits when only 7 needed
//                              11100000 100xxxxx (10xxxxxx) 16 bits when only 11 needed
//                              11110000 1000xxxx (10xxxxxx 10xxxxxx) 21 bits when only 16 needed
//                              11111000 10000xxx (10xxxxxx 10xxxxxx 10xxxxxx) 26 bits when only 21 needed
//                              11111100 100000xx (10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx) 31 bits when only 26 needed
//
//
// Overlong forms of characters (e.g., encoding a / character as 0xC0 0xAF
// instead of its correct 0x2F one-byte form) have been used to bypass security
// filters that only check for the standard byte sequence of a character.
//
// Code points U+D800 to U+DFFF (UTF-16 surrogates), "noncharacters" U+FDD0
// to U+FDEF reserved for internal use, and U+FFFE and U+FFFF must not occur in
// normal UTF-8 text.  UTF-8 decoders should treat them like malformed or overlong
// sequences for safety reasons.
//
// As described in http://www.cl.cam.ac.uk/~mgk25/ucs/examples/UTF-8-test.txt,
// we will follow convention, which is to replace any overlong or malformed characters
// with the replacement character, 0xfffd, and advance the read pointer past the end.

const   Unicode  ReplacementCharacter = 0xfffd;


// NOT PART OF THIS ASSIGNMENT
//
// To use Utf8 encoding for arbitrary 32-bit numbers, not text, extend the format
// to 7 bytes (36 bits) and skip the checks for characters that present security risks.


//
// UTF-16 Format
//


// Utf16 uses aligned 16-bit characters in either big-endian (high byte first)
// or little-endian (low byte first) format to represent Unicode values in
// the range from 0x00 to 0x10ffff (21 bits).

// Values 0x0000 to 0xffff are written as a single 16-bit character.
//
// Values > 0x10ffff (21 bits) are written as replacement characters.
//
// Values 0x10000 to 0x10ffff are written as two 16-bit surrogates.
//
// 1.  0x10000 is subtracted from the Unicode value.
// 2.  The High Surrogate is written first as 0xd800 + high 10 bits.
// 5.  The Low Surrogate is written second as 0xdc00 + low 10 bits.
// 6.  Unpaired / out-of-ordersurrogates are read as their literal 16-bit values.
//
// Reading consists of reversing the steps.


//
// Byte Order Marks (BOMs)
//


// Utf16 text files are are required to start with a Byte Order Mark (BOM) to
// indicate whether they are big-endian (high byte first) or little-endian
// (low byte first).
//
// If you read a correct BOM as the first 16-bit character in a file, it confirms
// you are reading it with the correct "endian-ness".  All Windows and Apple
// computers are little-endian, so that is what we will support.

const Unicode  ByteOrderMark = 0xfeff;

// If it's actually a big endian file, that first 16-bit character will have
// the two bytes flipped.  (To read big-endian text, simply flip the bytes
// before decoding.)

const Unicode  BigEndianBOM = 0xfffe;


// Utf8 text files do not need a BOM because they're written as a sequence
// of bytes.  Adding BOM to the start of a Utf8 file is permitted but
// discouraged.

// The Utf8 byte order mark is the same 0xfeff Unicode character value
// but written out as Utf8.

const Utf8     Utf8BOMString[ ] = { 0xef, 0xbb, 0xbf };


//
// UTF-8 Library Functions
//


// SizeOfUtf8 tells the number of bytes it will take to encode the
// specified value as Utf8.  Assumes values over 31 bits will be replaced.

// SizeOfUTF8( GetUtf8( p ) ) does not tell how many bytes encode
// the character pointed to by p because p may point to a malformed
// character.

size_t SizeOfUtf8( Unicode c );


// SizeOfUtf16 tells the number of bytes it will take to encode the
// specified value as Utf16. Assumes values over 0xffff will be written as
// surrogates and that values > 0x10ffff will be replaced.

size_t SizeOfUtf16( Unicode c );


// IndicatedLength looks at the first byte of a Utf8 sequence and
// determines the expected length.  Return 1 for an invalid first byte.

size_t IndicatedLength( const Utf8 *p );


// Read the next Utf8 character beginning at **pp and bump *pp past
// the end of the character.  if bound != null, bound points one past
// the last valid byte.
//
// 1.  If *pp already points to or past the last valid byte, do not
//     advance *pp and return the null character.
// 2.  If there is at least one byte, but the character runs past the
//     last valid byte or is invalid, return the replacement character
//     and set *pp pointing to just past the last byte consumed.
//
// Overlong characters and character values 0xd800 to 0xdfff (UTF-16
// surrogates), "noncharacters" 0xfdd0 to 0xfdef, and the values 0xfffe
// and 0xffff should be returned as the replacement character.

Unicode ReadUtf8( const Utf8 **p, const Utf8 *bound = nullptr );


// Scan backward for the first PREVIOUS byte which could
// be the start of a UTF-8 character.  If bound != null,
// bound points to the first (leftmost) valid byte.

const Utf8 *PreviousUtf8( const Utf8 *p, const Utf8 *bound = nullptr );


// Write a Unicode character in UTF-8, returning one past
// the the last byte that was written.
//
// If bound != null, it points one past the last valid location
// in the buffer. (If p is already at or past the bound, do
// nothing and return p.)
//
// If c > 0x7fffffff (31 bits) write the replacement character.

Utf8 *WriteUtf8( Utf8 *p, Unicode c, Utf8 *bound = nullptr );


// Read a Utf16 little-endian character beginning at **pp and
// bump *pp past the end of the character.  if bound != null,
// bound points one past the last valid byte.  If *pp >= bound,
// do not advance *pp and return the null character.
//
// Values over 16-bits are read as a high surrogate followed by
// a low surrogate. Unpaired or out-of-order surrogates are
// read as literal values.

Unicode ReadUtf16( const Utf16 **p, const Utf16 *bound = nullptr );


// Write a Unicode character in UTF-16, returning one past
// the the last byte that was written.
//
// If bound != null, it points one past the last valid location
// in the buffer. (If p is already at or past the bound, do
// nothing and return p.)
//
// If c > 0x10ffff (21 bits) write the replacement character.

Utf16 *WriteUtf16( Utf16 *p, Unicode c, Utf16 *bound = nullptr );


// Wrappers that read the character but don't advance the pointer.

Unicode GetUtf8( const Utf8 *p, const Utf8 *bound = nullptr );
Unicode GetUtf16( const Utf16 *p, const Utf16 *bound = nullptr );


// Wrappers that advance the pointer but throw away the value.

const Utf8 *NextUtf8( const Utf8 *p, const Utf8 *bound = nullptr );
const Utf16 *NextUtf16( const Utf16 *p, const Utf16 *bound = nullptr );
