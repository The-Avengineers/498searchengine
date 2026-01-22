#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "Utf8.h"
#include "doctest.h"

TEST_CASE("testing PreviousUtf8") {
  Utf8 flag[] = {0xf0, 0x9f, 0x8f, 0xb3, 0xef, 0xb8, 0x8f, 0xe2,
                 0x80, 0x8d, 0xf0, 0x9f, 0x8c, 0x88, 0x0a};
  Utf8 smiley[] = {0xF0, 0x9F, 0x98, 0x82};
  Utf8 letters_ab[] = {0x61, 0x62};

  CHECK(PreviousUtf8(&smiley[3]) == smiley);
  CHECK(PreviousUtf8(&smiley[1]) == smiley);
  CHECK(PreviousUtf8(smiley) == &smiley[-1]);
  CHECK(PreviousUtf8(letters_ab) == &letters_ab[-1]);
  CHECK(PreviousUtf8(&letters_ab[1]) == letters_ab);

  CHECK(PreviousUtf8(&smiley[3], smiley) == smiley);
  CHECK(PreviousUtf8(&smiley[1], smiley) == smiley);
}

TEST_CASE("testing ReadUtf16") {
  Utf16 smiley[] = {0xD83D, 0xDE02};
  Utf16 letter_a[] = {0x0061};
  Utf16 const *smiley_p = smiley;
  Utf16 const *smiley_n = smiley_p + 2;

  CHECK(GetUtf16(smiley_p) == 0x1F602);
  CHECK(ReadUtf16(&smiley_p) == 0x1F602);
  smiley_p -= 2;
  CHECK(ReadUtf16(&smiley_p, smiley_n) == 0x1F602);
  smiley_p -= 2;
  CHECK(ReadUtf16(&smiley_p, smiley_p + 1) == 0xD83D);
  smiley_p -= 2;
  CHECK(GetUtf16(&smiley[1]) == 0xDE02);
  CHECK(ReadUtf16(&smiley_p, smiley_p) == 0);

  Utf16 const *letter_a_p = letter_a;
  CHECK(ReadUtf16(&letter_a_p) == 0x00061);
  letter_a_p--;
  CHECK(ReadUtf16(&letter_a_p, letter_a_p) == 0x0);
}

TEST_CASE("testing ReadUtf8") {
  Utf8 smiley[] = {0xF0, 0x9F, 0x98, 0x82};
  Utf8 letter_a[] = {0x61};
  Utf8 umlaut_a[] = {0xC3, 0xA4};
  Utf8 manding[] = {0xDF, 0xBF};
  Utf8 const *smiley_p = smiley;
  Utf8 const *smiley_n = &smiley[4];

  CHECK(GetUtf8(smiley_p) == 0x1F602);
  CHECK(ReadUtf8(&smiley_p) == 0x1F602);
  smiley_p -= 4;
  CHECK(ReadUtf8(&smiley_p, smiley_n) == 0x1F602);
  smiley_p -= 4;

  CHECK(GetUtf8(umlaut_a) == 0x000E4);
  Utf8 const *umlaut_ap = umlaut_a;
  CHECK(ReadUtf8(&umlaut_ap) == 0x000E4);

  CHECK(GetUtf8(manding) == 0x07FF);
  Utf8 const *manding_p = manding;
  CHECK(ReadUtf8(&manding_p) == 0x07FF);

  Utf8 const *letter_a_p = letter_a;
  CHECK(ReadUtf8(&letter_a_p) == 0x61);
  CHECK(ReadUtf8(&letter_a_p, letter_a_p - 1) == 0);

  // From the stress test
  Utf8 impossible_bytes[] = {0xFE, 0x22, 0x22};
  CHECK(GetUtf8(impossible_bytes) == ReplacementCharacter);
  CHECK(NextUtf8(impossible_bytes) == &impossible_bytes[1]);

  CHECK(GetUtf8(&impossible_bytes[1]) == 0x22);
  CHECK(NextUtf8(&impossible_bytes[1]) == &impossible_bytes[2]);

  Utf8 impossible_bytes2[] = {0xFF, 0x22, 0x22};
  CHECK(GetUtf8(impossible_bytes2) == ReplacementCharacter);
  CHECK(NextUtf8(impossible_bytes2) == &impossible_bytes2[1]);

  Utf8 impossible_bytes_seq[] = {0xFE, 0xFE, 0xFF, 0xFF, 0x22, 0x22};
  CHECK(GetUtf8(impossible_bytes_seq) == ReplacementCharacter);
  CHECK(GetUtf8(&impossible_bytes_seq[1]) == ReplacementCharacter);
  CHECK(GetUtf8(&impossible_bytes_seq[2]) == ReplacementCharacter);
  CHECK(GetUtf8(&impossible_bytes_seq[3]) == ReplacementCharacter);

  CHECK(NextUtf8(impossible_bytes_seq) == &impossible_bytes_seq[1]);
  CHECK(NextUtf8(&impossible_bytes_seq[1]) == &impossible_bytes_seq[2]);
  CHECK(NextUtf8(&impossible_bytes_seq[2]) == &impossible_bytes_seq[3]);
  CHECK(NextUtf8(&impossible_bytes_seq[3]) == &impossible_bytes_seq[4]);

  Utf8 last_1_byte_sequence[] = {0x7F};
  CHECK(GetUtf8(last_1_byte_sequence) == 0x7F);

  Utf8 five_byte_start[] = {0xfc, 0x20};
  CHECK(GetUtf8(five_byte_start) == ReplacementCharacter);
}
