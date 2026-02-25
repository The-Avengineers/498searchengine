#include <avengine/ranker/ranker.h>

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
    const urlEntry& entry,
    const vector<vector<size_t>>& term_positions) // using insertion sort to dynamically pick top10
{
  RankedResult candidate;
  candidate.entry = entry;
  candidate.score = Score(entry, term_positions);

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

double Ranker::Score(const urlEntry& url_entry, const vector<vector<size_t>>& term_positions) const
{
  double combined = 0.0;

  for (size_t i = 0; i < Strategies.size(); ++i)
  {
    const WeightedStrategy& weighted = Strategies[i];
    if (weighted.strategy == nullptr)
    {
      return 0.0;
    }

    combined += weighted.weight * weighted.strategy->Score(url_entry, term_positions);
  }

  return combined;
}

} // namespace avengine
