TEMP_DIR=$(mktemp -d)
TEST_DIR="Testcases"

test_pair() {
  ./flip "$TEST_DIR/$1" > $TEMP_DIR/output.txt

  if cmp -s "$TEMP_DIR/output.txt" "$TEST_DIR/$2"; then
    echo -e "\x1b[32mPASSED\x1b[0m : $1 -> $2"
  else
    echo -e "\x1b[31mFAILED\x1b[0m : $1 -> $2"
  fi
}

test_pair GettysburgAddress.Utf16.txt GettysburgAddress.txt
test_pair GettysburgAddress.Utf16BE.txt GettysburgAddress.txt
test_pair GettysburgAddress.Utf8BOM.txt GettysburgAddress.Utf16.txt
test_pair GettysburgAddress.txt GettysburgAddress.Utf16.txt

test_pair KuhnStressTest.txt KuhnStressTest.Utf16.txt
test_pair KuhnStressTest.Utf16.txt KuhnStressTest.FlipTwice.txt
test_pair KuhnStressTest.Utf16BE.txt KuhnStressTest.FlipTwice.txt

test_pair PopularEmojis.txt PopularEmojis.Utf16.txt
test_pair PopularEmojis.Utf16.txt PopularEmojis.txt
test_pair PopularEmojis.Utf16BE.txt PopularEmojis.txt

test_pair VariousLanguages.txt VariousLanguages.Utf16.txt
test_pair VariousLanguages.Utf16.txt VariousLanguages.txt
test_pair VariousLanguages.Utf16BE.txt VariousLanguages.txt

