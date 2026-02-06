#include "HtmlTags.h"
#include <cassert>
#include <cstring>
#include <ctype.h>

// name points to beginning of the possible HTML tag name.
// nameEnd points to one past last character.
// Comparison is case-insensitive.
// Use a binary search.
// If the name is found in the TagsRecognized table, return
// the corresponding action.
// If the name is not found, return OrdinaryText.

// Case-insensitive compare between [name, nameEnd) and tag string.
// Returns <0 if name is "less", >0 if name is "greater", 0 if equal.
[[gnu::always_inline]] inline int compare(const char *name, const char *nameEnd,
                                          const char *tag) {
  while (name < nameEnd && *tag) {
    unsigned char n = static_cast<unsigned char>(*name);
    unsigned char t = static_cast<unsigned char>(*tag);
    int n_lower = ::tolower(n);
    int t_lower = ::tolower(t);
    if (n_lower != t_lower) {
      return n_lower - t_lower;
    }
    ++name;
    ++tag;
  }
  if (name < nameEnd) {
    return 1;
  }
  if (*tag) {
    return -1;
  }
  return 0;
}

// binary search to find a tag
// requires start and end pointer for the name and the start pointer for tag
// modifys nothing
// returns desired action for the correct tag
DesiredAction LookupPossibleTag(const char *name, const char *nameEnd) {

  if (!nameEnd) {
    nameEnd = name + strlen(name);
  }
  int l = 0;
  int r = NumberOfTags - 1;

  while (l <= r) {
    int m = l + r;
    m /= 2;
    const char *tag = TagsRecognized[m].Tag;

    int val = compare(name, nameEnd, tag);
    if (val == 0) {
      return TagsRecognized[m].Action;
    } else if (val < 0) {
      r = m - 1;
    } else {
      l = m + 1;
    }
  }

  return DesiredAction::OrdinaryText;
}
