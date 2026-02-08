#include "avengine/vector.h"
#include <cstddef>

enum class PhraseKind
{
  TITLE,
  NORMAL,
};

struct Phrase
{
  char* text;
  PhraseKind kind;
};

// Puts the list of words into the currently active index. url must in it's
// canonical form. Words is a list of pointers to cstrings in a single
// continuous block of memory. The text in pharses must be writable and
// monotonically increasing, i.e phrases[i].text < phrases.text[i + 1].
size_t indexDocument(char const* url, char* pharses);
