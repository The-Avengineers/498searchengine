#include <avengine/Utf8.h>
#include <avengine/normalization.h>
#include <cstring>

// Returns true if c is an ASCII alphabetic character.
static bool is_ascii_alpha(unsigned char c)
{
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

// Returns the lowercase version of an ASCII letter; non-letters are unchanged.
static char deaccent_utf8(unsigned char lead, unsigned char cont)
{
  int i = cont & 0x3F; // continuation byte → 0..63
  switch (lead)
  {

  case 0xC3:
  { // U+00C0–U+00FF  (Latin-1 Supplement)
    static const char t[64] = {
        'A', 'A', 'A', 'A', 'A', 'A', 'A', 'C', // À Á Â Ã Ä Å Æ Ç
        'E', 'E', 'E', 'E', 'I', 'I', 'I', 'I', // È É Ê Ë Ì Í Î Ï
        'D', 'N', 'O', 'O', 'O', 'O', 'O', 0,   // Ð Ñ Ò Ó Ô Õ Ö ×
        'O', 'U', 'U', 'U', 'U', 'Y', 0,   0,   // Ø Ù Ú Û Ü Ý Þ ß
        'a', 'a', 'a', 'a', 'a', 'a', 'a', 'c', // à á â ã ä å æ ç
        'e', 'e', 'e', 'e', 'i', 'i', 'i', 'i', // è é ê ë ì í î ï
        'd', 'n', 'o', 'o', 'o', 'o', 'o', 0,   // ð ñ ò ó ô õ ö ÷
        'o', 'u', 'u', 'u', 'u', 'y', 0,   'y', // ø ù ú û ü ý þ ÿ
    };
    return t[i];
  }

  case 0xC4:
  { // U+0100–U+013F  (Latin Extended-A, first half)
    static const char t[64] = {
        'A', 'a', 'A', 'a', 'A', 'a', 'C', 'c', // Ā ā Ă ă Ą ą Ć ć
        'C', 'c', 'C', 'c', 'C', 'c', 'D', 'd', // Ĉ ĉ Ċ ċ Č č Ď ď
        'D', 'd', 'E', 'e', 'E', 'e', 'E', 'e', // Đ đ Ē ē Ĕ ĕ Ė ė
        'E', 'e', 'E', 'e', 'G', 'g', 'G', 'g', // Ę ę Ě ě Ĝ ĝ Ğ ğ
        'G', 'g', 'G', 'g', 'H', 'h', 'H', 'h', // Ġ ġ Ģ ģ Ĥ ĥ Ħ ħ
        'I', 'i', 'I', 'i', 'I', 'i', 'I', 'i', // Ĩ ĩ Ī ī Ĭ ĭ Į į
        'I', 'i', 0,   0,   'J', 'j', 'K', 'k', // İ ı Ĳ ĳ Ĵ ĵ Ķ ķ
        'k', 'L', 'l', 'L', 'l', 'L', 'l', 'L', // ĸ Ĺ ĺ Ļ ļ Ľ ľ Ŀ
    };
    return t[i];
  }

  case 0xC5:
  { // U+0140–U+017F  (Latin Extended-A, second half)
    static const char t[64] = {
        'l', 'L', 'l', 'N', 'n', 'N', 'n', 'N', // ŀ Ł ł Ń ń Ņ ņ Ň
        'n', 'n', 0,   0,   'O', 'o', 'O', 'o', // ň ŉ Ŋ ŋ Ō ō Ŏ ŏ
        'O', 'o', 0,   0,   'R', 'r', 'R', 'r', // Ő ő Œ œ Ŕ ŕ Ŗ ŗ
        'R', 'r', 'S', 's', 'S', 's', 'S', 's', // Ř ř Ś ś Ŝ ŝ Ş ş
        'S', 's', 'T', 't', 'T', 't', 'T', 't', // Š š Ţ ţ Ť ť Ŧ ŧ
        'U', 'u', 'U', 'u', 'U', 'u', 'U', 'u', // Ũ ũ Ū ū Ŭ ŭ Ů ů
        'U', 'u', 'U', 'u', 'W', 'w', 'Y', 'y', // Ű ű Ų ų Ŵ ŵ Ŷ ŷ
        'Y', 'Z', 'z', 'Z', 'z', 'Z', 'z', 's', // Ÿ Ź ź Ż ż Ž ž ſ
    };
    return t[i];
  }

  default:
    return 0; // unmapped
  }
}
// Normalizes a single word in-place: folds accented UTF-8 letters to ASCII,
// replaces non-alpha ASCII bytes with spaces, and lowercases everything.
// The slice is updated to reflect any length change from multi-byte → single-byte folds.
static void normalizeWord(avengine::string_slice& word, size_t& accentCount)
{
  size_t j = 0;

  for (size_t i = 0; i < word.size(); i++)
  {
    if (i + 1 < word.size() && (word[i] & 0xE0) == 0xC0)
    {
      char base = deaccent_utf8(word[i], word[i + 1]);
      if (base)
      {
        word[j++] = tolower(base);
        accentCount++;
        i++;
        continue;
      }
    }

    if (is_ascii_alpha(word[i]))
    {
      word[j++] = tolower(word[i]);
      continue;
    }

    word[j++] = ' ';
  }

  word = avengine::string_slice(word.begin(), j);
}

bool tooMuchNonAscii(avengine::vector<avengine::string_slice>& words)
{
  size_t nonAsciiCount = 0;
  size_t length = 0;

  for (avengine::string_slice& word : words)
  {
    length += word.size();

    for (size_t i = 0; i < word.size(); ++i)
    {
      if ((static_cast<unsigned char>(word[i]) & 0x80) != 0)
      {
        ++nonAsciiCount;
      }
    }
  }

  return nonAsciiCount * 2 > length;
}

avengine::RejectionReason
avengine::normalizeAndReject(avengine::vector<avengine::string_slice>& words)
{
  size_t length = 0;

  for (auto& word : words)
  {
    length += word.size();
  }

  if (tooMuchNonAscii(words))
  {
    return RejectionReason::TOO_MUCH_NONASCII;
  }

  size_t accentCount = 0;

  for (avengine::string_slice& word : words)
  {
    normalizeWord(word, accentCount);
  }

  if (accentCount * 2 > length)
  {
    return RejectionReason::TOO_MANY_ACCENTS;
  }

  return RejectionReason::NOT_REJECTED;
}
