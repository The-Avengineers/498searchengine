#ifndef RANKER_H_
#define RANKER_H_

#include <cstddef>

#include <avengine/string.h>
#include <avengine/vector.h>

namespace avengine {

/**
 * Ranker combines one or more scoring strategies into a single score.
 * Ranker is supposed to rank one url at one time, we have the information of all the positions 
 * of the matching query words
 */
class Ranker {
public:
  /**
   * Strategy interface for scoring a single URL with query term positions.
   */
  class Strategy {
  public:
    virtual ~Strategy() = default;
    virtual double Score(
        const string &url,
        const vector<vector<size_t>> &term_positions) const = 0;
  };

  /**
   * Add a non-owning strategy pointer with an optional weight.
   */
  void AddStrategy(const Strategy *strategy, double weight = 1.0);

  /**
   * Remove all strategies.
   */
  void ClearStrategies();

  /**
   * Score a single URL using all strategies.
   */
  double Score(const string &url,
               const vector<vector<size_t>> &term_positions) const;

private:
  struct WeightedStrategy {
    const Strategy *strategy { nullptr };
    double weight { 1.0 };
  };

  vector<WeightedStrategy> Strategies;
};

} // namespace avengine

#endif /* RANKER_H_ */
