#include "ranker.h"
#include <avengine/ranker/ranking_detail.h>
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
  static constexpr double short_thres = 10.0;
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
    (void)term_positions;
    const double body   = signals.cache.body_exact_phrase   ? 1.0 : 0.0;
    const double title  = signals.cache.title_exact_phrase  ? 1.0 : 0.0;
    const double anchor = signals.cache.anchor_exact_phrase ? 1.0 : 0.0;
    return (0.3 * body) + (0.3 * title) + (0.4 * anchor);
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
    (void)term_positions;
    return (0.3 * signals.cache.body_ordering)
         + (0.3 * signals.cache.title_ordering)
         + (0.4 * signals.cache.anchor_ordering);
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
  // number of link clicks from seed to get to page
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

class SeedDomainDepth : public Ranker::Strategy
{
  // ranks pages based on how many domain switches from seed
  // eg. wikipedia to wikipedia is better than wikipedia to reddit despite url structure
public:
  double Score(const urlEntry& entry, const vector<vector<size_t>>& term_positions,
               const Ranker::RankSignals& signals) const
  {
    (void)term_positions;
    (void)signals;
    const double d = static_cast<double>(entry.seedDomainDepth);
    if (d <= shallow_thres)
      return 1.0;
    if (d >= deep_thres)
      return 0.0;
    return (deep_thres - d) / (deep_thres - shallow_thres);
  }

private:
  static constexpr double shallow_thres = 1.0;
  static constexpr double deep_thres = 5.0;
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
  // prefer URLs that contain more query terms, with a smaller extra bonus when
  // those terms also appear in-order and close together.
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

    size_t present_terms = 0;
    for (size_t i = 0; i < query_len; ++i)
    {
      if (QueryTerms[i].size() != 0 && url.find(QueryTerms[i].cstr(), 0) != avengine::string::npos)
        ++present_terms;
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
      return static_cast<double>(present_terms) / static_cast<double>(query_len);
    }

    const double presence_ratio =
        static_cast<double>(present_terms) / static_cast<double>(query_len);
    const double length_ratio = static_cast<double>(best_len) / static_cast<double>(query_len);
    const double proximity = 1.0 / (1.0 + static_cast<double>(best_gap));
    const double ordered_bonus = length_ratio * proximity;
    return 0.6 * presence_ratio + 0.4 * ordered_bonus;
  }

private:
  vector<avengine::string> QueryTerms;
};

class UrlPathDepth : public Ranker::Strategy
{
  // Scores based on number of path segments after the domain.
  // Top-level pages (e.g. example.com/about) score higher than deeply nested ones.
  // 1.0 for 0-1 segments, linearly drops to 0.0 at max_depth segments.
public:
  double Score(const urlEntry& entry, const vector<vector<size_t>>& term_positions,
               const Ranker::RankSignals& signals) const
  {
    (void)term_positions;
    (void)signals;
    const avengine::string& url = entry.url;

    // Find start of path (first '/' after scheme "://")
    size_t path_start = avengine::string::npos;
    const size_t scheme_sep = url.find("://");
    if (scheme_sep != avengine::string::npos)
      path_start = url.find("/", scheme_sep + 3);

    if (path_start == avengine::string::npos)
      return 1.0; //  root domain

    // Count non-empty path segments (e.g. "nytimes.com/" = 0, "nytimes.com/newsletters" = 1)
    size_t segments = 0;
    size_t i = path_start;
    while (i < url.size())
    {
      if (url[i] == '/')
        ++i;
      else
      {
        ++segments;
        while (i < url.size() && url[i] != '/')
          ++i;
      }
    }

    if (segments == 0)
      return 1.0;
    if (segments >= max_depth)
      return 0.0;
    return static_cast<double>(max_depth - segments) / static_cast<double>(max_depth);
  }

private:
  static constexpr size_t max_depth = 6;
};

class TitleTermCoverage : public Ranker::Strategy
{
  // Scores purely on how many query terms appear in the title,
  // plus ordering and exact-phrase bonuses
  // Body positions are ignored so this weight can be
  // independently tunable from the body-based strategies.
public:
  double Score(const urlEntry& entry, const vector<vector<size_t>>& term_positions,
               const Ranker::RankSignals& signals) const
  {
    (void)entry;
    (void)term_positions;
    if (signals.cache.title_coverage == 0.0)
      return 0.0;
    return (0.15 * signals.cache.title_coverage)
         + (0.15 * signals.cache.title_ordering)
         + (0.40 * (signals.cache.title_exact_phrase ? 1.0 : 0.0))
         + (0.30 * signals.cache.title_early);
  }
};

class ExactTitleMatch : public Ranker::Strategy
{
  // all query terms appear as a consecutive phrase in the title and the title has no extra
  // words
public:
  explicit ExactTitleMatch(const vector<avengine::string>& query_terms)
      : QueryTermCount(query_terms.size())
  {
  }

  double Score(const urlEntry& entry, const vector<vector<size_t>>& term_positions,
               const Ranker::RankSignals& signals) const
  {
    (void)entry;
    (void)term_positions;
    if (QueryTermCount == 0 || signals.cache.title_coverage != 1.0 || !signals.cache.title_exact_phrase)
      return 0.0;
    return signals.title_word_count == QueryTermCount ? 1.0 : 0.0;
  }

private:
  size_t QueryTermCount{0};
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
    (void)term_positions;
    return (0.3 * signals.cache.body_early)
         + (0.3 * signals.cache.title_early)
         + (0.4 * signals.cache.anchor_early);
  }
};

class QueryParamPenalty : public Ranker::Strategy
{
  // Returns 1.0 when the URL contains a "q=" query parameter
public:
  double Score(const urlEntry& entry, const vector<vector<size_t>>& term_positions,
               const Ranker::RankSignals& signals) const
  {
    (void)term_positions;
    (void)signals;
    const avengine::string& url = entry.url;
    if (url.find("?q=") != avengine::string::npos || url.find("&q=") != avengine::string::npos ||
        url.find("&amp;q=") != avengine::string::npos)
      return 1.0;
    return 0.0;
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
    (void)term_positions;
    return (0.3 * signals.cache.body_coverage)
         + (0.3 * signals.cache.title_coverage)
         + (0.4 * signals.cache.anchor_coverage);
  }
};
