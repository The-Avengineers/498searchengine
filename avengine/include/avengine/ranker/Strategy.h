#include "ranker.h"
#include <avengine/HtmlParser.h>
#include <avengine/avengine_lib/string.h>
#include <avengine/avengine_lib/vector.h>

#include <cmath>
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
  // TODO: scores the pages according to their distance from the seedlist (communicate with crawler)
public:
  double Score(const avengine::string& url, const vector<vector<size_t>>& term_positions) const {}
};

class ExactPhraseMatch : public Ranker::Strategy
{
  // scores the pages based on the exact matching of query phrases
  // checks if current indices form phrase
  // runs in O(sum_of_term_appearances)
public:
  double Score(const avengine::string& url, const vector<vector<size_t>>& term_positions) const
  {
    (void)url;

    if (term_positions.size() < 2)
      return 1.0;

    for (const auto& positions : term_positions)
    {
      if (positions.empty())
        return 0.0;
    }

    size_t n = term_positions.size();
    avengine::vector<size_t> indices(n, 0);
    avengine::vector<size_t> absolute(n, 0);

    for (size_t i = 0; i < n; ++i)
    {
      absolute[i] = term_positions[i][0];
    }

    while (true)
    {
      size_t min_term = 0;
      size_t min_val = absolute[0];
      bool is_phrase = true;
      for (size_t i = 1; i < n; ++i)
      {
        if (absolute[i] != absolute[0] + i)
        {
          is_phrase = false;
        }

        if (absolute[i] + min_term < min_val + i)
        {
          min_val = absolute[i];
          min_term = i;
        }
      }

      if (is_phrase)
      {
        return 1.0;
      }

      indices[min_term]++;

      if (indices[min_term] >= term_positions[min_term].size())
      {
        return 0.0;
      }

      absolute[min_term] += term_positions[min_term][indices[min_term]];
    }
  }
};

class CorrectOrdering : public Ranker::Strategy
{
  // reward longer in-order runs of query terms and favor tighter spans.
  // Score scales up with ordered run length and down with extra gap between terms.
public:
  double Score(const avengine::string& url, const vector<vector<size_t>>& term_positions) const
  {
    (void)url;

    const size_t query_len = term_positions.size();
    if (query_len == 0)
    {
      return 0.0;
    }
    if (query_len == 1)
    {
      return 1.0;
    }

    vector<vector<size_t>> abs_positions;
    abs_positions = BuildAbsolutePositions(term_positions);

    size_t best_len = 0;
    size_t best_span = 0;

    for (size_t start_term = 0; start_term < query_len; ++start_term)
    {
      const vector<size_t>& starts = abs_positions[start_term];
      if (starts.size() == 0)
      {
        continue;
      }

      for (size_t si = 0; si < starts.size(); ++si)
      {
        size_t start_pos = starts[si];
        size_t prev_pos = start_pos;
        size_t end_pos = start_pos;
        size_t len = 1;

        for (size_t term_idx = start_term + 1; term_idx < query_len; ++term_idx)
        {
          const vector<size_t>& positions = abs_positions[term_idx];
          if (positions.size() == 0)
          {
            break;
          }

          size_t next_pos = 0;
          bool found = false;
          for (size_t pi = 0; pi < positions.size(); ++pi)
          {
            if (positions[pi] > prev_pos)
            {
              next_pos = positions[pi];
              found = true;
              break;
            }
          }

          if (!found)
          {
            break;
          }

          prev_pos = next_pos;
          end_pos = next_pos;
          ++len;
        }

        size_t span = end_pos - start_pos;
        if (len > best_len || (len == best_len && span < best_span))
        {
          best_len = len;
          best_span = span;
        }
      }
    }

    if (best_len < 2)
    {
      return 0.0;
    }

    const double length_ratio = static_cast<double>(best_len) / static_cast<double>(query_len);
    const double length_weight = length_ratio * length_ratio;
    const size_t min_span = best_len - 1;
    const size_t slack = best_span > min_span ? (best_span - min_span) : 0;
    const double proximity = 1.0 / (1.0 + static_cast<double>(slack));

    return length_weight * proximity;
  }

private:
  vector<vector<size_t>> BuildAbsolutePositions(const vector<vector<size_t>>& term_positions) const
  {
    vector<vector<size_t>> abs_positions(term_positions.size());

    for (size_t i = 0; i < term_positions.size(); ++i)
    {
      const vector<size_t>& rel_positions = term_positions[i];
      vector<size_t> abs_list;
      size_t absolute = 0;

      for (size_t j = 0; j < rel_positions.size(); ++j)
      {
        absolute += rel_positions[j];
        abs_list.pushBack(absolute);
      }

      abs_positions[i] = abs_list;
    }

    return abs_positions;
  }
};

class OutlinkCount : public Ranker::Strategy
{
  // TODO: scores the pages based on the number of hyperlinks outwards
public:
  double Score(const avengine::string& url, const vector<vector<size_t>>& term_positions) const {}
};

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
};

class HttpsMatch : public Ranker::Strategy
{
  // scores page based on https vs http
  double Score(const avengine::string& url, const vector<vector<size_t>>& term_positions) const
  {
    return (url.size() >= 8 && url[4] == 's') ? 1.0 : 0.0;
  }
};

class QueryOccurrence : public Ranker::Strategy
{
  // prefer URLs that contain longer in-order query term runs with smaller gaps.
  // Longer matches boost relevance; scattered matches are downweighted.
public:
  explicit QueryOccurrence(const vector<avengine::string>& query_terms) : QueryTerms(query_terms) {}

  double Score(const avengine::string& url, const vector<vector<size_t>>& term_positions) const
  {
    (void)term_positions;

    const size_t query_len = QueryTerms.size();
    if (query_len == 0)
    {
      return 0.0;
    }

    size_t best_len = 0;
    size_t best_gap = 0;

    for (size_t start_term = 0; start_term < query_len; ++start_term)
    {
      const avengine::string& first_term = QueryTerms[start_term];
      if (first_term.size() == 0)
      {
        continue;
      }

      size_t start_pos = const_cast<avengine::string&>(url).find(first_term.cstr(), 0);
      while (start_pos != avengine::string::npos)
      {
        size_t len = 1;
        size_t gap_sum = 0;
        size_t prev_end = start_pos + first_term.size();

        for (size_t term_idx = start_term + 1; term_idx < query_len; ++term_idx)
        {
          const avengine::string& term = QueryTerms[term_idx];
          if (term.size() == 0)
          {
            continue;
          }

          size_t next_pos = const_cast<avengine::string&>(url).find(term.cstr(), prev_end);
          if (next_pos == avengine::string::npos)
          {
            break;
          }

          if (next_pos > prev_end)
          {
            gap_sum += (next_pos - prev_end);
          }

          prev_end = next_pos + term.size();
          ++len;
        }

        if (len > best_len || (len == best_len && gap_sum < best_gap))
        {
          best_len = len;
          best_gap = gap_sum;
        }

        start_pos = const_cast<avengine::string&>(url).find(first_term.cstr(), start_pos + 1);
      }
    }

    if (best_len == 0)
    {
      return 0.0;
    }

    const double length_ratio = static_cast<double>(best_len) / static_cast<double>(query_len);
    const double length_weight = length_ratio * length_ratio;
    const double proximity = 1.0 / (1.0 + static_cast<double>(best_gap));

    return length_weight * proximity;
  }

private:
  vector<avengine::string> QueryTerms;
};

class EarlyQueryTermMatch : public Ranker::Strategy
{
  // Scores how early each term appears in document on log scale
  // TODO: add bonus if terms are in title, need that parameter from crawler/index
public:
  double Score(const avengine::string& url, const vector<vector<size_t>>& term_positions) const
  {
    (void)url;
    if (term_positions.empty())
    {
      return 0.0;
    }

    double score = 0.0;
    size_t valid_terms = 0;
    for (const auto& positions : term_positions)
    {
      if (!positions.empty())
      {
        score += 1.0 / log2(2.0 + positions[0]);
        ++valid_terms;
      }
    }
    return score / valid_terms;
  }
};
