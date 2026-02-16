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
  const avengine::vector<const char*> GoodDomains = {".edu",
                                                     ".gov"}; // Add more good domains in here
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

class DistanceFromSeeds : public Ranker::Strategy
{
  // scores the pages according to their distance from the seedlist (communicate with crawler)
public:
  double Score(const avengine::string& url, const vector<vector<size_t>>& term_positions) const {}
};

class ExactPhraseMatch : public Ranker::Strategy
{
  // scores the pages based on the exact matching of query phrases
  // 1 word queries return 1, missing term queries return 0
  // runtime is O(1st_word_occurances * (query_len - 1) * avg_word_occurances)
public:
  double Score(const avengine::string& url, const vector<vector<size_t>>& term_positions) const
  {
    (void)url;

    if (term_position.size() < 2)
      return 1.0;

    for (const auto& positions : term_positions)
    {
      if (positions.empty())
        return 0.0;
    }

    size_t first_absolute = 0;
    for (size_t rel_pos : term_positions[0])
    {

      first_absolute += rel_pos;

      bool is_phrase = true;

      for (size_t term_idx = 1; term_idx < term_positions.size(); ++term_idx)
      {
        size_t expected_pos = first_absolute + term_idx;
        if (!HasPositionAt(expected_pos, term_positions[term_idx]))
        {
          is_phrase = false;
          break;
        }

        if (is_phrase)
          return 1.0;
      }
    }
    return 0.0;
  }

private:
  bool HasPositionAt(size_t target_pos, avengine::vector<size_t>& relative_positions)
  {
    size_t absolute_pos = 0;
    for (size_t rel_pos : relative_positions)
    {
      absolute_pos += rel_pos;
      if (absolute_pos == target_pos)
      {
        return true;
      }
      if (absolute_pos > target_pos)
      {
        return false;
      }
    }
    return false;
  }
};

class CorrectOrdering : public Ranker::Strategy
{
  // scores the pages based on the ordering of the query words in the pages
public:
  double Score(const avengine::string& url, const vector<vector<size_t>>& term_positions) const {}
}

class OutlinkCount : public Ranker::Strategy
{
  // scores the pages based on the numebr of hyperlinks outwards
public:
  double Score(const avengine::string& url, const vector<vector<size_t>>& term_positions) const {}
}

class UrlDepth : public Ranker::Strategy
{
  // scores the pages based on reciprocal '/' count in URL, assuming all url have at least 2 in
  // 'https://'
public:
  double Score(const avengine::string& url, const vector<vector<size_t>>& term_positions) const
  {
    size_t slash_count = 0;
    for (char c : url)
    {
      if (c == '/')
        ++slash_count;
    }
    return slash_count == 0 ? 0.0 : 1 / static_cast<double>(slash_count);
  }
}

class HttpsMatch : public Ranker::Strategy
{
  // scores page based on https vs http
  double Score(const avengine::string& url, const vector<vector<size_t>>& term_positions) const
  {
    return (url.size() >= 8 && url[4] == 's') ? 1.0 : 0.0;
  }
}
