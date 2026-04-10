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
  unordered_map<uint8_t> emptyMembership;
  swap(RetainedUrls_, emptyMembership);
  insertionCount_ = 0;
}

void Ranker::Consider(const urlEntry& entry, const string& title,
                      const vector<vector<size_t>>& term_positions, const RankSignals& signals,
                      size_t shardOrdinal)
{
  if (MaxResults_ == 0)
  {
    return;
  }

  const uint8_t* retained = RetainedUrls_.get(string_slice(entry.url.cstr()));
  if (retained && *retained != 0)
    return;

  // When full, skip scoring entirely if this candidate can't beat the weakest result.
  const double new_score = Score(entry, term_positions, signals);
  if (Top10.size() == MaxResults_ && new_score <= Top10.top().score)
    return;

  RankedResult candidate;
  candidate.entry = entry;
  candidate.title = title;
  candidate.score = new_score;
  candidate.shardOrdinal = shardOrdinal;
  candidate.insertionIndex = insertionCount_++;

  Top10.push(candidate);
  RetainedUrls_.insert(entry.url, 1u);

  if (Top10.size() > MaxResults_)
  {
    const RankedResult evicted = Top10.top();
    Top10.pop();
    RetainedUrls_.insert(evicted.entry.url, 0u);
  }
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
    result.pushBack(ascending[i - 1]);
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
