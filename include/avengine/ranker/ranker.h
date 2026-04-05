#ifndef RANKER_H_
#define RANKER_H_

#include <cstddef>

#include <avengine/avengine_lib/string.h>
#include <avengine/avengine_lib/vector.h>
#include <avengine/crawler/crawler_structs.h>

namespace avengine
{

/**
 * Ranker combines one or more scoring strategies into a single score.
 * Ranker is supposed to rank one url at one time, we have the information of all the positions
 * of the matching query words
 *
 * Dynamically output the Top 10 results:
 * using insertion sort (in this scenario faster than other sorting algos)
 */
class Ranker
{
public:
  explicit Ranker(size_t maxResults = 10);

  struct RankSignals
  {
    size_t outlink_count{0};
    size_t title_word_count{0};
    const vector<vector<size_t>>* title_term_positions{nullptr};
  };

  /**
   * Strategy interface for scoring a single URL with query term positions.
   *
   *
   * List of heuristics:
   * Static:
   *
   * static rank
        1. Some domains are better than others, e.g., .gov or .edu over
        .biz.
        2. Short URLs are better.
        3. Short titles are probably better.
        4. Some pages may be obvious spam.
        5. Some pages may have lots of other pages pointing to them,
        e.g., PageRank.26

   *
   * Dynamic:
   */
  class Strategy
  {
  public:
    virtual ~Strategy() = default;
    virtual double Score(const urlEntry& entry, const vector<vector<size_t>>& term_positions,
                         const RankSignals& signals) const = 0;
  };

  /**
   * Add a non-owning strategy pointer with an optional weight.
   */
  void AddStrategy(const Strategy* strategy, double weight = 1.0);

  /**
   * Change the maximum number of retained results.
   */
  void SetMaxResults(size_t maxResults);

  /**
   * Remove all strategies.
   */
  void ClearStrategies();

  /**
   * resets the retained top-k results list before next query
   */
  void ResetResults();

  /**
   * Score a single URL using all strategies.
   */
  double Score(const urlEntry& entry, const vector<vector<size_t>>& term_positions,
               const RankSignals& signals) const;

  struct RankedResult
  {
    urlEntry entry;
    string title;
    double score{0.0};
  };

  /**
   * Consider a URL and update the top results using insertion sort.
   */
  void Consider(const urlEntry& entry, const string& title,
                const vector<vector<size_t>>& term_positions, const RankSignals& signals);

  /**
   * Return the current top results (highest score first).
   */
  const vector<RankedResult>& TopResults() const;

private:
  struct WeightedStrategy
  {
    const Strategy* strategy{nullptr};
    double weight{1.0};
  };

  vector<WeightedStrategy> Strategies;
  vector<RankedResult> TopResults_;
  size_t MaxResults_ = 10;
};

class ConstantScoreStrategy : public Ranker::Strategy
{
public:
  double Score(const urlEntry& entry, const vector<vector<size_t>>& term_positions,
               const Ranker::RankSignals& signals) const override
  {
    (void)entry;
    (void)term_positions;
    (void)signals;
    return 1.0;
  }
};

} // namespace avengine

#endif /* RANKER_H_ */
