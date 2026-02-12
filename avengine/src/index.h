#include <cstddef>

// Puts the list of words into the currently active index. url must in it's
// canonical form. Words is a list of pointers to cstrings in a single
// continuous block of memory. **words must be writable and monotonically
// increasing, i.e word[i] < words[i + 1].
size_t index_document(char const* url, char* words);
