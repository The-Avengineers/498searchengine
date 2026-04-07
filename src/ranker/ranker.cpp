#include <avengine/ranker/ranker.h>

namespace avengine
{

Ranker::Ranker(size_t maxResults)
{
  SetMaxResults(maxResults);
}

void Ranker::AddStrategy(const Strategy* strategy, double weight)
{
  WeightedStrategy entry;
  entry.strategy = strategy;
  entry.weight = weight;
  Strategies.pushBack(entry);
}

void Ranker::SetMaxResults(size_t maxResults)
{
  MaxResults_ = maxResults == 0 ? 10 : maxResults;
}

void Ranker::ClearStrategies()
{
  Strategies.clear();
}

void Ranker::ResetResults()
{
  TopResults_.clear();
}

void Ranker::Consider(const urlEntry& entry, const string& title,
                      const vector<vector<size_t>>& term_positions,
                      const RankSignals& signals) // keep a bounded top-k with insertion sort
{
  if (MaxResults_ == 0)
  {
    return;
  }

  RankedResult candidate;
  candidate.entry = entry;
  candidate.title = title;
  candidate.score = Score(entry, term_positions, signals);

  size_t insert_pos = 0;
  while (insert_pos < TopResults_.size() && TopResults_[insert_pos].score >= candidate.score)
  {
    ++insert_pos;
  }

  if (TopResults_.size() >= MaxResults_ && insert_pos >= MaxResults_)
    return;

  TopResults_.insert(insert_pos, candidate);

  if (TopResults_.size() > MaxResults_)
    TopResults_.popBack();
}

const vector<Ranker::RankedResult>& Ranker::TopResults() const
{
  return TopResults_;
}

double Ranker::Score(const urlEntry& url_entry, const vector<vector<size_t>>& term_positions,
                     const RankSignals& signals) const
{
  double combined = 0.0;

  for (size_t i = 0; i < Strategies.size(); ++i)
  {
    const WeightedStrategy& weighted = Strategies[i];
    if (weighted.strategy == nullptr)
    {
      return 0.0;
    }

    combined += weighted.weight * weighted.strategy->Score(url_entry, term_positions, signals);
  }

  return combined;
}

} // namespace avengine
