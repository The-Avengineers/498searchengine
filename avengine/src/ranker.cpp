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

void Ranker::Consider(
    const string& url,
    const vector<vector<size_t>>& term_positions) // using insertion sort to dynamically pick top10
{
  RankedResult candidate;
  candidate.url = url;
  candidate.score = Score(url, term_positions);

  size_t insert_pos = 0;
  while (insert_pos < Top10.size() && Top10[insert_pos].score >= candidate.score)
  {
    ++insert_pos;
  }

  Top10.insert(insert_pos, candidate);

  if (Top10.size() > 10)
  {
    Top10.popBack();
  }
}

const vector<Ranker::RankedResult>& Ranker::TopResults() const
{
  return Top10;
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
