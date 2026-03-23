#include <avengine/normalization.h>

#include <avengine/avengine_lib/string_slice.h>
#include <avengine/avengine_lib/vector.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace
{
void fail(const char* expr, int line)
{
  fprintf(stderr, "normalization_test failed at line %d: %s\n", line, expr);
  abort();
}

#define EXPECT(expr)                                                                               \
  do                                                                                               \
  {                                                                                                \
    if (!(expr))                                                                                   \
      fail(#expr, __LINE__);                                                                       \
  } while (false)

bool containsWord(const avengine::vector<avengine::string_slice>& words, const char* target)
{
  for (size_t i = 0; i < words.size(); ++i)
  {
    if (words[i] == avengine::string_slice(target))
    {
      return true;
    }
  }
  return false;
}
} // namespace

int main()
{
  // Normalization: lowercase + remove non-alpha + accent folding.
  // CAFé naïve fiancé C++ 123
  char w1[] = "CAF\xc3\xa9";
  char w2[] = "na\xc3\xafve";
  char w3[] = "fianc\xc3\xa9";
  char w4[] = "C++";
  char w5[] = "123";
  avengine::vector<avengine::string_slice> words;
  words.pushBack(avengine::string_slice(w1, strlen(w1)));
  words.pushBack(avengine::string_slice(w2, strlen(w2)));
  words.pushBack(avengine::string_slice(w3, strlen(w3)));
  words.pushBack(avengine::string_slice(w4, strlen(w4)));
  words.pushBack(avengine::string_slice(w5, strlen(w5)));

  avengine::RejectionReason r = avengine::normalizeAndReject(words);
  EXPECT(r == avengine::RejectionReason::NOT_REJECTED);
  EXPECT(containsWord(words, "cafe"));
  EXPECT(containsWord(words, "naive"));
  EXPECT(containsWord(words, "fiance"));
  EXPECT(!containsWord(words, "123"));

  // Accent folding should also work for uppercase accented letters.
  // ÉLAN NIÑO
  char u1[] = "\xC3\x89LAN";
  char u2[] = "NI\xC3\x91O";
  avengine::vector<avengine::string_slice> wordsUpper;
  wordsUpper.pushBack(avengine::string_slice(u1, strlen(u1)));
  wordsUpper.pushBack(avengine::string_slice(u2, strlen(u2)));

  avengine::RejectionReason r2 = avengine::normalizeAndReject(wordsUpper);
  EXPECT(r2 == avengine::RejectionReason::NOT_REJECTED);
  EXPECT(containsWord(wordsUpper, "elan"));
  EXPECT(containsWord(wordsUpper, "nino"));

  // Reject pages with too many non-ASCII bytes (>50%).
  // 18 non-ASCII bytes (6 CJK characters × 3 bytes each) + 3 ASCII = >50% non-ASCII.
  char nonAscii[] = "\xE4\xB8\xAD\xE6\x96\x87\xE4\xB8\xAD\xE6\x96\x87\xE3\x81\x82\xE3\x81\x84"
                    "abc";
  avengine::vector<avengine::string_slice> nonAsciiWords;
  nonAsciiWords.pushBack(avengine::string_slice(nonAscii, strlen(nonAscii)));

  avengine::RejectionReason r3 = avengine::normalizeAndReject(nonAsciiWords);
  EXPECT(r3 == avengine::RejectionReason::TOO_MUCH_NONASCII);

  return 0;
}
