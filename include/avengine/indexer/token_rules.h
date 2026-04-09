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

// Index tokens are normalized to ASCII letters and digits only.
static inline bool is_index_token_char(unsigned char c)
{
  return is_index_alpha(c) || is_index_digit(c);
}

static inline bool index_token_digits_allowed(size_t digitCount)
{
  return digitCount <= MAX_INDEX_TOKEN_DIGITS;
}

static inline unsigned char lower_index_ascii(unsigned char c)
{
  if (c >= 'A' && c <= 'Z')
  {
    return static_cast<unsigned char>(c + ('a' - 'A'));
  }
  return c;
}

static inline bool matches_lower_literal(const char* start, size_t len, const char* literal)
{
  size_t i = 0;
  for (; i < len && literal[i] != '\0'; ++i)
  {
    if (lower_index_ascii(static_cast<unsigned char>(start[i])) !=
        static_cast<unsigned char>(literal[i]))
    {
      return false;
    }
  }
  return i == len && literal[i] == '\0';
}

// Hyphens are normally treated as separators, except for a small set of
// common prefixed compounds (e.g., "re-use", "non-profit", "co-op").
static inline bool is_common_hyphen_prefix(const char* start, const char* end)
{
  const size_t len = static_cast<size_t>(end - start);
  return matches_lower_literal(start, len, "co") || matches_lower_literal(start, len, "de") ||
         matches_lower_literal(start, len, "re") || matches_lower_literal(start, len, "un") ||
         matches_lower_literal(start, len, "mid") || matches_lower_literal(start, len, "non") ||
         matches_lower_literal(start, len, "pre") || matches_lower_literal(start, len, "pro") ||
         matches_lower_literal(start, len, "sub") || matches_lower_literal(start, len, "anti") ||
         matches_lower_literal(start, len, "over") || matches_lower_literal(start, len, "post") ||
         matches_lower_literal(start, len, "semi") || matches_lower_literal(start, len, "inter") ||
         matches_lower_literal(start, len, "multi") || matches_lower_literal(start, len, "super") ||
         matches_lower_literal(start, len, "trans") || matches_lower_literal(start, len, "ultra") ||
         matches_lower_literal(start, len, "under");
}

static inline bool is_index_apostrophe(unsigned char c)
{
  return c == '\'';
}

} // namespace avengine
