#include <avengine/ranker.h>

namespace avengine
{

void Ranker::AddStrategy(const Strategy* strategy, double weight)
{
  WeightedStrategy entry;
  entry.strategy = strategy;
  entry.weight = weight;
  Strategies.pushBack(entry);
}

void Ranker::ClearStrategies()
{
  Strategies.clear();
}

double Ranker::Score(const string& url, const vector<vector<size_t>>& term_positions) const
{
  double combined = 0.0;

  for (size_t i = 0; i < Strategies.size(); ++i)
  {
    const WeightedStrategy& entry = Strategies[i];
    if (entry.strategy == nullptr)
    {
      return 0.0;
    }

    combined += entry.weight * entry.strategy->Score(url, term_positions);
  }

  return combined;
}

} // namespace avengine
