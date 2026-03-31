#include "ranker.h"
#include <avengine/avengine_lib/string.h>
#include <avengine/avengine_lib/vector.h>
#include <avengine/crawler/crawler_structs.h>

#include <cmath>
#include <cstddef>

using namespace avengine;

class DomainStrat : public Ranker::Strategy
{
public:
  double Score(const urlEntry& entry, const vector<vector<size_t>>& term_positions,
               const Ranker::RankSignals& signals) const
  {
    (void)term_positions;
    (void)signals;
    const avengine::string& domain = entry.domain;
    for (size_t i = 0; i < TopTier.size(); i++)
    {
      if (domain.find(TopTier[i]) != avengine::string::npos)
        return 1.0;
    }
    for (size_t i = 0; i < MidTier.size(); i++)
    {
      if (domain.find(MidTier[i]) != avengine::string::npos)
        return 0.6;
    }
    for (size_t i = 0; i < LowTier.size(); i++)
    {
      if (domain.find(LowTier[i]) != avengine::string::npos)
        return 0.3;
    }
    return 0.15;
  }

private:
  const avengine::vector<const char*> TopTier = {".edu", ".gov", ".org"};
  const avengine::vector<const char*> MidTier = {".com"};
  const avengine::vector<const char*> LowTier = {".xyz", ".net", ".io"};
};

class ShortURLStrat : public Ranker::Strategy
{
  // piecewise linear: 1.0 for urls <= short_thres, linearly drops to 0.0 at long_thres
public:
  double Score(const urlEntry& entry, const vector<vector<size_t>>& term_positions,
               const Ranker::RankSignals& signals) const
  {
    (void)term_positions;
    (void)signals;
    const double len = static_cast<double>(entry.url.size());
    if (len <= short_thres)
      return 1.0;
    if (len >= long_thres)
      return 0.0;
    return (long_thres - len) / (long_thres - short_thres);
  }

private:
  static constexpr double short_thres = 40.0;
  static constexpr double long_thres = 120.0;
};

class ShortTitleStrat : public Ranker::Strategy
{
  // piecewise linear: 1.0 for titles <= short_thres words, linearly drops to 0.0 at long_thres
public:
  double Score(const urlEntry& entry, const vector<vector<size_t>>& term_positions,
               const Ranker::RankSignals& signals) const
  {
    (void)entry;
    (void)term_positions;
    const double words = static_cast<double>(signals.title_word_count);
    if (words <= short_thres)
      return 1.0;
    if (words >= long_thres)
      return 0.0;
    return (long_thres - words) / (long_thres - short_thres);
  }

private:
  static constexpr double short_thres = 5.0;
  static constexpr double long_thres = 15.0;
};

class ExactPhraseMatch : public Ranker::Strategy
{
  // scores the pages based on the exact matching of query phrases
  // checks if current indices form phrase
  // runs in O(sum_of_term_appearances)
public:
  double Score(const urlEntry& entry, const vector<vector<size_t>>& term_positions,
               const Ranker::RankSignals& signals) const
  {
    (void)entry;
    (void)signals;

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
  double Score(const urlEntry& entry, const vector<vector<size_t>>& term_positions,
               const Ranker::RankSignals& signals) const
  {
    (void)entry;
    (void)signals;

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
    const size_t min_span = best_len - 1;
    const size_t slack = best_span > min_span ? (best_span - min_span) : 0;
    const double proximity = 1.0 / (1.0 + static_cast<double>(slack));

    return length_ratio * proximity;
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
  // scores pages based on outgoing-link count using a capped log scale.
public:
  double Score(const urlEntry& entry, const vector<vector<size_t>>& term_positions,
               const Ranker::RankSignals& signals) const
  {
    (void)entry;
    (void)term_positions;
    if (signals.outlink_count == 0)
    {
      return 0.0;
    }

    const double score =
        log2(1.0 + static_cast<double>(signals.outlink_count)) /
        log2(33.0); // some math trick: pages w/ more than 32 outlinks all have score 1.0
    return score >= 1.0 ? 1.0 : score;
  }
};

class UrlDepth : public Ranker::Strategy
{
  // piecewise linear: 1.0 for depth <= shallow_thres, linearly drops to 0.0 at deep_thres
public:
  double Score(const urlEntry& entry, const vector<vector<size_t>>& term_positions,
               const Ranker::RankSignals& signals) const
  {
    (void)term_positions;
    (void)signals;
    const double d = static_cast<double>(entry.depth);
    if (d <= shallow_thres)
      return 1.0;
    if (d >= deep_thres)
      return 0.0;
    return (deep_thres - d) / (deep_thres - shallow_thres);
  }

private:
  static constexpr double shallow_thres = 2.0;
  static constexpr double deep_thres = 8.0;
};

// HttpsMatch is intentionally disabled because the crawler only indexes HTTPS URLs.
// class HttpsMatch : public Ranker::Strategy
// {
//   double Score(const urlEntry& entry,
//                const vector<vector<size_t>>& term_positions,
//                const Ranker::RankSignals& signals) const
//   {
//     (void)term_positions;
//     (void)signals;
//     return (entry.url.size() >= 8 && entry.url[4] == 's') ? 1.0 : 0.0;
//   }
// };

class QueryOccurrence : public Ranker::Strategy
{
  // prefer URLs that contain longer in-order query term runs with smaller gaps.
  // Longer matches boost relevance; scattered matches are downweighted.
public:
  explicit QueryOccurrence(const vector<avengine::string>& query_terms) : QueryTerms(query_terms) {}

  double Score(const urlEntry& entry, const vector<vector<size_t>>& term_positions,
               const Ranker::RankSignals& signals) const
  {
    (void)term_positions;
    (void)signals;
    const avengine::string& url = entry.url;

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

      size_t start_pos = url.find(first_term.cstr(), 0);
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

          size_t next_pos = url.find(term.cstr(), prev_end);
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

        start_pos = url.find(first_term.cstr(), start_pos + 1);
      }
    }

    if (best_len == 0)
    {
      return 0.0;
    }

    const double length_ratio = static_cast<double>(best_len) / static_cast<double>(query_len);
    const double proximity = 1.0 / (1.0 + static_cast<double>(best_gap));

    return length_ratio * proximity;
  }

private:
  vector<avengine::string> QueryTerms;
};

class EarlyQueryTermMatch : public Ranker::Strategy
{
  // Scores how early each term appears in document on log scale
  // TODO: add bonus if terms are in title, need that parameter from crawler/index
public:
  double Score(const urlEntry& entry, const vector<vector<size_t>>& term_positions,
               const Ranker::RankSignals& signals) const
  {
    (void)entry;
    const size_t query_len = term_positions.size();
    if (query_len == 0)
    {
      return 0.0;
    }

    double body_score = 0.0;
    size_t valid_terms = 0;
    for (const auto& positions : term_positions)
    {
      if (!positions.empty())
      {
        body_score += 1.0 / log2(2.0 + positions[0]);
        ++valid_terms;
      }
    }

    body_score = (valid_terms > 0) ? body_score / valid_terms : 0.0;

    if (signals.title_term_positions == nullptr)
    {
      return body_score;
    }

    const vector<vector<size_t>>& title_positions = *signals.title_term_positions;
    size_t matched_title_terms = 0;
    for (size_t i = 0; i < query_len; ++i)
    {
      if (i < title_positions.size() && !title_positions[i].empty())
      {
        ++matched_title_terms;
      }
    }

    const double title_bonus =
        0.25 * static_cast<double>(matched_title_terms) / static_cast<double>(query_len);
    const double combined = body_score + title_bonus;
    return combined >= 1.0 ? 1.0 : combined;
  }
};

class TermCoverage : public Ranker::Strategy
{
  // scores pages by the fraction of query terms that appear anywhere in document
  // A document matching all query terms scores 1.0 and matching no terms scores 0.0
public:
  double Score(const urlEntry& entry, const vector<vector<size_t>>& term_positions,
               const Ranker::RankSignals& signals) const
  {
    (void)entry;
    (void)signals;
    const size_t query_len = term_positions.size();
    if (query_len == 0)
    {
      return 0.0;
    }

    size_t present = 0;
    for (const auto& positions : term_positions)
    {
      if (!positions.empty())
      {
        ++present;
      }
    }

    return static_cast<double>(present) / static_cast<double>(query_len);
  }
};
