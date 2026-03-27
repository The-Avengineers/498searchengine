#include <avengine/html_parser/HtmlParser.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace
{
void fail(const char* expr, int line)
{
  fprintf(stderr, "crawler_html_parser_test failed at line %d: %s\n", line, expr);
  abort();
}

#define EXPECT(expr)                                                                               \
  do                                                                                               \
  {                                                                                                \
    if (!(expr))                                                                                   \
      fail(#expr, __LINE__);                                                                       \
  } while (false)

bool strEq(const avengine::string_slice& lhs, const char* rhs)
{
  return lhs == avengine::string_slice(rhs);
}

bool strEq(const avengine::string& lhs, const char* rhs)
{
  return lhs == avengine::string(rhs);
}

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
  // Basic crawler parsing
  const char html[] = "<html><head><title>Search Engine</title>"
                      "<base href=\"https://base.example/root/\"></head>"
                      "<body><a href=\"/docs/page\">Read More</a>"
                      "<embed src=\"https://cdn.example/file.pdf\">"
                      "<script>hidden text</script>"
                      "visible text</body></html>";

  avengine::HtmlParser parser(html, strlen(html));

  // Base URL
  EXPECT(strEq(parser.base, "https://base.example/root/"));

  // Title words
  EXPECT(parser.titleWords.size() == 2);
  EXPECT(strEq(parser.titleWords[0], "search"));
  EXPECT(strEq(parser.titleWords[1], "engine"));
  EXPECT(strEq(parser.fullTitle, "search engine"));

  // Links and anchor text
  EXPECT(parser.links.size() == 2);
  EXPECT(strEq(parser.links[0].URL, "/docs/page"));
  EXPECT(parser.links[0].anchorText.size() == 2);
  EXPECT(strEq(parser.links[0].anchorText[0], "read"));
  EXPECT(strEq(parser.links[0].anchorText[1], "more"));
  EXPECT(strEq(parser.links[1].URL, "https://cdn.example/file.pdf"));
  EXPECT(parser.links[1].anchorText.size() == 0);

  // Words
  EXPECT(containsWord(parser.words, "visible"));
  EXPECT(containsWord(parser.words, "text"));
  EXPECT(!containsWord(parser.words, "hidden"));

  // Edge cases: first base wins, case-insensitive tags/attrs, missing href anchor,
  // and comment/script/style/svg content should be discarded.
  const char html2[] = "<HTML><HEAD>"
                       "<BASE HREF=\"https://first.example/root/\">"
                       "<base href=\"https://second.example/root/\">"
                       "<TITLE>UPPER Title</TITLE>"
                       "</HEAD><BODY>"
                       "<!-- hidden comment words -->"
                       "<A>no href anchor</A>"
                       "<A HREF=\"/Keep\">Keep Text</A>"
                       "<STYLE>style words should disappear</STYLE>"
                       "<SVG>svg words should disappear</SVG>"
                       "<SCRIPT>script words should disappear</SCRIPT>"
                       "body words"
                       "</BODY></HTML>";

  avengine::HtmlParser parser2(html2, strlen(html2));

  // First base
  EXPECT(strEq(parser2.base, "https://first.example/root/"));

  // Title
  EXPECT(parser2.titleWords.size() == 2);
  EXPECT(strEq(parser2.titleWords[0], "upper"));
  EXPECT(strEq(parser2.titleWords[1], "title"));
  EXPECT(strEq(parser2.fullTitle, "upper title"));

  // Only one valid anchor link (the <A HREF=...> one).
  EXPECT(parser2.links.size() == 1);
  EXPECT(strEq(parser2.links[0].URL, "/Keep"));
  EXPECT(parser2.links[0].anchorText.size() == 2);
  EXPECT(strEq(parser2.links[0].anchorText[0], "keep"));
  EXPECT(strEq(parser2.links[0].anchorText[1], "text"));

  // Words
  EXPECT(containsWord(parser2.words, "no"));
  EXPECT(containsWord(parser2.words, "href"));
  EXPECT(containsWord(parser2.words, "anchor"));
  EXPECT(containsWord(parser2.words, "body"));
  EXPECT(containsWord(parser2.words, "words"));
  EXPECT(!containsWord(parser2.words, "hidden"));
  EXPECT(!containsWord(parser2.words, "style"));
  EXPECT(!containsWord(parser2.words, "svg"));
  EXPECT(!containsWord(parser2.words, "script"));

  // Normalization: lowercase + remove non-alpha + accent folding.
  const char html3[] =
      "<html><body>CAF\xc3\xa9 na\xc3\xafve fianc\xc3\xa9 co-op C++ 123</body></html>";
  avengine::HtmlParser parser3(html3, strlen(html3));
  EXPECT(!parser3.rejected);
  EXPECT(containsWord(parser3.words, "cafe"));
  EXPECT(containsWord(parser3.words, "naive"));
  EXPECT(containsWord(parser3.words, "fiance"));
  EXPECT(containsWord(parser3.words, "coop"));
  EXPECT(containsWord(parser3.words, "c"));
  EXPECT(!containsWord(parser3.words, "123"));

  // Accent folding should also work for uppercase accented letters.
  const char htmlUpperAccents[] = "<html><body>\xC3\x89LAN NI\xC3\x91O</body></html>";
  avengine::HtmlParser parserUpperAccents(htmlUpperAccents, strlen(htmlUpperAccents));
  EXPECT(!parserUpperAccents.rejected);
  EXPECT(containsWord(parserUpperAccents.words, "elan"));
  EXPECT(containsWord(parserUpperAccents.words, "nino"));

  // Reject pages with too many non-ASCII bytes (>50%).
  const char html4[] = "\xE4\xB8\xAD\xE6\x96\x87\xE4\xB8\xAD\xE6\x96\x87\xE3\x81\x82\xE3\x81\x84"
                       "abc";
  avengine::HtmlParser parser4(html4, strlen(html4));
  EXPECT(parser4.rejected);
  EXPECT(parser4.words.size() == 0);
  EXPECT(parser4.titleWords.size() == 0);
  EXPECT(strEq(parser4.fullTitle, ""));
  EXPECT(parser4.links.size() == 0);

  // Query normalization
  avengine::string query =
      avengine::HtmlParser::queryNormalize("  \xE4\xB8\xAD\xE6\x96\x87  NI\xC3\x91O   ");
  EXPECT(strEq(query, "nino"));

  return 0;
}
