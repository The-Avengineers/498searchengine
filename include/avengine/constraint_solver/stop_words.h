#pragma once
#include <cstring>

#include <avengine/avengine_lib/string.h>

namespace avengine
{
namespace constraint_solver
{

// Returns true if the term is a common english  word.
inline bool isStopWord(const string& term)
{
  static const char* const kStopWords[] = {
    "a", "about", "after", "again", "all", "also",
    "am", "an", "and", "any", "are", "as", "at",
    "be",  "but", "by",
    "can", "could",
    "did", "do", "does", "doing", "down",
    "each",
    "few", "for", "from",
    "get", "got",
    "had", "has", "have", "having", "he", "her", "here", "him", "his", "how",
    "i", "if", "in", "into", "is", "it", "its",
    "just",
    "me", "more", "most", "my",
    "no", "not", "now",
    "of", "off", "on", "only", "or", "other", "our", "out", "over", "own",
    "same", "she", "should", "so", "some", "such",
    "than", "that", "the", "their", "them", "then", "there", "these", "they",
    "this", "those", "to", "too",
    "under", "until", "up",
    "very",
    "was", "we", "were", "what", "when", "where", "which", "while", "who",
    "whom", "why", "will", "with", "would",
    "you", "your",
  };

  constexpr size_t kCount = sizeof(kStopWords) / sizeof(kStopWords[0]);
  const char* data = term.cstr();

  size_t lo = 0, hi = kCount;
  while (lo < hi)
  {
    const size_t mid = lo + (hi - lo) / 2;
    const int cmp = std::strcmp(data, kStopWords[mid]);
    if (cmp == 0)
      return true;
    if (cmp < 0)
      hi = mid;
    else
      lo = mid + 1;
  }
  return false;
}

} // namespace constraint_solver
} // namespace avengine
