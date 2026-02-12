#include <avengine/HtmlParser.h>
#include <avengine/avengine_lib/string.h>
#include <avengine/avengine_lib/vector.h>
#include <avengine/ranker.h>

#include <cstddef>
#include <fstream>
#include <string>

using namespace avengine;

class DomainStrat : public Ranker::Strategy
{
public:
  double Score(const avengine::string& url, const vector<vector<size_t>>& term_positions) const
  {
    (void)term_positions;
    // Implementation: e.g., check if URL contains ".edu"
    avengine::string url_copy = url;
    for (size_t i = 0; i < GoodDomains.size(); i++)
    {
      if (url_copy.find(GoodDomains[i]) != avengine::string::npos)
      {
        return 1.0;
      }
    }
    return 0.0;
  }

private:
  const avengine::vector<const char*> GoodDomains = {".edu", ".gov"};
};

class ShortURLStrat : public Ranker::Strategy
{
  // naïve implementation: take the reciprocal of the length of url
public:
  double Score(const avengine::string& url, const vector<vector<size_t>>& term_positions) const
  {
    (void)term_positions;
    return url.size() == 0 ? 0.0 : 1 / double(url.size());
  }
};

class ShortTitleStrat : public Ranker::Strategy
{
public:
  double Score(const avengine::string& url, const vector<vector<size_t>>& term_positions) const
  {
    (void)term_positions;
    return getTitleLen(url) <= length_thres ? 1.0 : 0.0;
  }

private:
  size_t getTitleLen(const avengine::string& url) const
  {
    std::ifstream in(url.cstr(), std::ios::binary);
    if (!in)
    {
      return length_thres + 1;
    }

    std::string buffer((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (buffer.empty())
    {
      return length_thres + 1;
    }

    avengine::HtmlParser parser(buffer.data(), buffer.size());
    return parser.titleWords.size();
  }

  const size_t length_thres = 10;
};
