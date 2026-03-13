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

bool strEq(const avengine::string& lhs, const char* rhs)
{
  return strcmp(lhs.cstr(), rhs) == 0;
}

bool containsWord(const avengine::vector<avengine::string>& words, const char* target)
{
  for (size_t i = 0; i < words.size(); ++i)
  {
    if (strcmp(words[i].cstr(), target) == 0)
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
  EXPECT(strEq(parser.titleWords[0], "Search"));
  EXPECT(strEq(parser.titleWords[1], "Engine"));

  // Links and anchor text
  EXPECT(parser.links.size() == 2);
  EXPECT(strEq(parser.links[0].URL, "/docs/page"));
  EXPECT(parser.links[0].anchorText.size() == 2);
  EXPECT(strEq(parser.links[0].anchorText[0], "Read"));
  EXPECT(strEq(parser.links[0].anchorText[1], "More"));
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
  EXPECT(strEq(parser2.titleWords[0], "UPPER"));
  EXPECT(strEq(parser2.titleWords[1], "Title"));

  // Only one valid anchor link (the <A HREF=...> one).
  EXPECT(parser2.links.size() == 1);
  EXPECT(strEq(parser2.links[0].URL, "/Keep"));
  EXPECT(parser2.links[0].anchorText.size() == 2);
  EXPECT(strEq(parser2.links[0].anchorText[0], "Keep"));
  EXPECT(strEq(parser2.links[0].anchorText[1], "Text"));

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

  return 0;
}
