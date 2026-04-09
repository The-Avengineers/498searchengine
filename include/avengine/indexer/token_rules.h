#pragma once

#include <cstddef>

namespace avengine
{

constexpr size_t MAX_INDEX_TOKEN_DIGITS = 4;

static inline bool is_index_alpha(unsigned char c)
{
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

static inline bool is_index_digit(unsigned char c)
{
  return c >= '0' && c <= '9';
}

// Index tokens may contain ASCII letters and digits. Tokens are kept only if
// they contain at most MAX_INDEX_TOKEN_DIGITS digits total.
static inline bool is_index_token_char(unsigned char c)
{
  return is_index_alpha(c) || is_index_digit(c);
}

static inline bool index_token_digits_allowed(size_t digitCount)
{
  return digitCount <= MAX_INDEX_TOKEN_DIGITS;
}

} // namespace avengine
