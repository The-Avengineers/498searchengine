#include <avengine/ranker/ranker.h>
#include <avengine/ranker/ranking_detail.h>

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

  // Results without a URL are not usable in the UI and they poison URL-based
  // dedup by collapsing many broken hits onto the same empty key.
  if (entry.url.size() == 0)
    return;

  const uint8_t* retained = RetainedUrls_.get(string_slice(entry.url.cstr()));
  if (retained && *retained != 0)
    return;

  // Quality pre-filter: skip documents that have no query terms in their title
  // and no exact phrase match in the body
  {
    using namespace ranking_strategy_detail;
    const bool title_covered = signals.title_term_positions != nullptr &&
                               CoverageScore(*signals.title_term_positions) > 0.0;
    const bool anchor_covered = signals.anchor_term_positions != nullptr &&
                                CoverageScore(*signals.anchor_term_positions) > 0.0;
    if (!title_covered && !anchor_covered && !HasExactPhrase(term_positions))
      return;
  }

  // When full, skip scoring entirely if this candidate can't beat the weakest result.
  vector<double> per_strategy;
  const double new_score = ScoreDetailed(entry, term_positions, signals, per_strategy);
  if (Top10.size() == MaxResults_ && new_score <= Top10.top().score)
    return;

  RankedResult candidate;
  candidate.entry = entry;
  candidate.title = title.size() == 0 ? entry.url : title;
  candidate.score = new_score;
  candidate.strategy_scores = per_strategy;
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
  // Copy the raw backing vector directly (avoids copying the full heap object + drain).
  const vector<RankedResult>& raw = Top10.serializeHeap();
  vector<RankedResult> result;
  result.reserve(raw.size());
  for (size_t i = 0; i < raw.size(); ++i)
    result.pushBack(raw[i]);

  // Sort descending by score; on ties, earlier insertions (lower index) come first.
  result.sort(
      [](const RankedResult& a, const RankedResult& b)
      {
        if (a.score != b.score)
          return a.score > b.score;
        return a.insertionIndex < b.insertionIndex;
      });
  return result;
}

double Ranker::ScoreDetailed(const urlEntry& url_entry,
                             const vector<vector<size_t>>& term_positions,
                             const RankSignals& signals, vector<double>& out_scores) const
{
  // Precompute all detail-function results once so strategies can read from
  // signals.cache instead of recomputing the same values independently.
  RankSignals cached = signals;
  using namespace ranking_strategy_detail;

  cached.cache.body_coverage     = CoverageScore(term_positions);
  cached.cache.body_ordering     = OrderingScore(term_positions);
  cached.cache.body_early        = EarlyMatchScore(term_positions);
  cached.cache.body_exact_phrase = HasExactPhrase(term_positions);

  if (signals.title_term_positions != nullptr)
  {
    const auto& tp = *signals.title_term_positions;
    cached.cache.title_coverage = CoverageScore(tp);
    if (cached.cache.title_coverage > 0.0)
    {
      cached.cache.title_ordering     = OrderingScore(tp);
      cached.cache.title_early        = EarlyMatchScore(tp);
      cached.cache.title_exact_phrase = HasExactPhrase(tp);
    }
  }

  if (signals.anchor_term_positions != nullptr)
  {
    const auto& ap = *signals.anchor_term_positions;
    cached.cache.anchor_coverage = CoverageScore(ap);
    if (cached.cache.anchor_coverage > 0.0)
    {
      cached.cache.anchor_ordering     = OrderingScore(ap);
      cached.cache.anchor_early        = EarlyMatchScore(ap);
      cached.cache.anchor_exact_phrase = HasExactPhrase(ap);
    }
  }

  double combined = 0.0;
  out_scores.clear();

  for (size_t i = 0; i < Strategies.size(); ++i)
  {
    const WeightedStrategy& weighted = Strategies[i];
    if (weighted.strategy == nullptr)
    {
      out_scores.pushBack(0.0);
      continue;
    }
    const double s = weighted.weight * weighted.strategy->Score(url_entry, term_positions, cached);
    out_scores.pushBack(s);
    combined += s;
  }

  return combined;
}

double Ranker::Score(const urlEntry& url_entry, const vector<vector<size_t>>& term_positions,
                     const RankSignals& signals) const
{
  vector<double> ignored;
  return ScoreDetailed(url_entry, term_positions, signals, ignored);
}

} // namespace avengine
