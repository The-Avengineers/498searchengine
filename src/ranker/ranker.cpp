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
  Top10.clear();
  insertionCount_ = 0;
}

void Ranker::Consider(const urlEntry& entry, const string& title,
                      const vector<vector<size_t>>& term_positions, const RankSignals& signals)
{
  if (MaxResults_ == 0)
  {
    return;
  }

  // Deduplicate: skip URLs already tracked since same URL can appear across shards.
  const vector<RankedResult>& heap = Top10.serializeHeap();
  for (size_t i = 0; i < heap.size(); ++i)
  {
    if (heap[i].entry.url == entry.url)
      return;
  }

  // When full, skip scoring entirely if this candidate can't beat the weakest result.
  const double new_score = Score(entry, term_positions, signals);
  if (Top10.size() == MaxResults_ && new_score <= Top10.top().score)
    return;

  RankedResult candidate;
  candidate.entry = entry;
  candidate.title = title;
  candidate.score = new_score;
  candidate.insertionIndex = insertionCount_++;

  Top10.push(candidate);

  if (Top10.size() > MaxResults_)
    Top10.pop();
}

vector<Ranker::RankedResult> Ranker::TopResults() const
{
  // Copy the heap and drain it (pops ascending: worst first).
  priorityQueue<RankedResult, greater<RankedResult>> copy = Top10;
  vector<RankedResult> ascending;
  while (!copy.empty())
  {
    ascending.pushBack(copy.top());
    copy.pop();
  }

  // Reverse to return highest score first.
  vector<RankedResult> result;
  for (size_t i = ascending.size(); i > 0; --i)
  {
    result.pushBack(ascending[i - 1]);
  }
  return result;
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
