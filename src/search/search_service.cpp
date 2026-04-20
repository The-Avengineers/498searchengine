#include <avengine/search/search_service.h>

#include <avengine/avengine_lib/thread.h>
#include <avengine/ranker/Strategy.h>
#include <avengine/ranker/ranker.h>
#include <avengine/ranker/ranking_detail.h>
#include <avengine/search/coordinator_merge.h>
#include <avengine/search/index_shard_source.h>
#include <avengine/search/search_common.h>
#include <avengine/search/serving_shard_source.h>
#include <avengine/unordered_map.h>

#include <avengine/avengine_lib/atomic.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <unistd.h>

namespace avengine
{
namespace search
{
namespace
{

using SteadyTimePoint = std::chrono::steady_clock::time_point;

size_t DurationToMs(std::chrono::steady_clock::duration duration);
size_t ComputeWindowStart(size_t page, size_t limit);
size_t NormalizeResultLimit(size_t limit);

bool HasSfiSuffix(const string& name)
{
  return name.size() >= 4 && name[name.size() - 4] == '.' && name[name.size() - 3] == 's' &&
         name[name.size() - 2] == 'f' && name[name.size() - 1] == 'i';
}

bool HasSsiSuffix(const string& name)
{
  return name.size() >= 4 && name[name.size() - 4] == '.' && name[name.size() - 3] == 's' &&
         name[name.size() - 2] == 's' && name[name.size() - 1] == 'i';
}

bool IsAnchorShard(const string& path)
{
  size_t nameStart = 0;
  for (size_t i = path.size(); i > 0; --i)
  {
    if (path[i - 1] == '/')
    {
      nameStart = i;
      break;
    }
  }
  const char marker[] = "anchor_";
  const size_t markerLen = 7;
  const size_t nameLen = path.size() - nameStart;
  if (nameLen < markerLen)
    return false;

  bool startsWithAnchor = true;
  for (size_t i = 0; i < markerLen; ++i)
  {
    if (path[nameStart + i] != marker[i])
    {
      startsWithAnchor = false;
      break;
    }
  }
  if (startsWithAnchor)
    return true;

  // Aggregated runtime shards can be prefixed by machine, e.g.
  // A_anchor_00511.sfi. Keep those out of the primary constraint solver too.
  for (size_t i = nameStart; i + markerLen < path.size(); ++i)
  {
    if (path[i] != '_')
      continue;

    bool hasAnchorMarker = true;
    for (size_t j = 0; j < markerLen; ++j)
    {
      if (path[i + 1 + j] != marker[j])
      {
        hasAnchorMarker = false;
        break;
      }
    }
    if (hasAnchorMarker)
      return true;
  }

  return false;
}

// Returns the registered domain from a URL.
// https://maps.google.com/path -> "google.com"

string ExtractRegisteredDomain(const string& url)
{
  // Find start of hostname after "://" and end of hostname at '/', '?', or ':'
  size_t host_start = 0;
  const size_t scheme_sep = url.find("://");
  if (scheme_sep != string::npos)
    host_start = scheme_sep + 3;

  size_t host_end = url.size();
  for (size_t i = host_start; i < url.size(); ++i)
  {
    if (url[i] == '/' || url[i] == '?' || url[i] == ':')
    {
      host_end = i;
      break;
    }
  }

  size_t last_dot = string::npos;
  for (size_t i = host_start; i < host_end; ++i)
    if (url[i] == '.')
      last_dot = i;

  if (last_dot == string::npos)
  {
    string result;
    for (size_t i = host_start; i < host_end; ++i)
      result.pushBack(url[i]);
    return result;
  }

  size_t second_last_dot = string::npos;
  for (size_t i = host_start; i < last_dot; ++i)
    if (url[i] == '.')
      second_last_dot = i;

  const size_t domain_start = second_last_dot != string::npos ? second_last_dot + 1 : host_start;
  string result;
  for (size_t i = domain_start; i < host_end; ++i)
    result.pushBack(url[i]);
  return result;
}

string JoinPath(const string& directory, const string& filename)
{
  string path = directory;
  if (path.size() != 0 && path[path.size() - 1] != '/')
    path.pushBack('/');
  path += filename;
  return path;
}

vector<string> DiscoverShardPaths(const string& directory)
{
  vector<string> servingPaths;
  vector<string> legacyPaths;
  DIR* dir = opendir(directory.cstr());
  if (!dir)
    return legacyPaths;

  while (dirent* entry = readdir(dir))
  {
    if (entry->d_name[0] == '.')
      continue;

    const string name(entry->d_name);
    if (HasSsiSuffix(name))
      servingPaths.pushBack(JoinPath(directory, name));
    else if (HasSfiSuffix(name))
      legacyPaths.pushBack(JoinPath(directory, name));
  }

  closedir(dir);
  // without sorting, the same query could return different top
  // results across runs just because shard enumeration changed
  servingPaths.sort();
  legacyPaths.sort();
  return servingPaths.empty() ? legacyPaths : servingPaths;
}

struct MatchPositions
{
  vector<vector<size_t>> termPositions; // termPositions[termIdx] = locations in document
};

class QueryLocalShardSource : public constraint_solver::IsrSource
{
public:
  explicit QueryLocalShardSource(const IndexShardSource* shard) : shard_(shard) {}

  ISRWord* OpenTermIsr(const string& term) const override
  {
    return shard_ ? shard_->OpenTermIsr(term) : nullptr;
  }

  ISREndDoc* OpenEndDocIsr() const override
  {
    return shard_ ? shard_->OpenEndDocIsr() : nullptr;
  }

  string GetDocumentURL(uint32_t docId) const override
  {
    return shard_ ? shard_->GetDocumentURL(docId) : string();
  }

private:
  const IndexShardSource* shard_ = nullptr;
};

struct ShardQueryContext
{
  explicit ShardQueryContext(const IndexShardSource* source = nullptr, size_t ordinal = 0)
      : shard(source), shardOrdinal(ordinal), querySource(source)
  {
  }

  const IndexShardSource* shard = nullptr;
  size_t shardOrdinal = 0;
  QueryLocalShardSource querySource;
  constraint_solver::SolveResult solved;
  vector<MatchPositions> bodyPositions;
  vector<MatchPositions> titlePositions;
  vector<MatchPositions> anchorPositions;
};

struct PooledRankedHit
{
  Ranker::RankedResult ranked;
  size_t shardOrdinal = 0;
  string description;
};

struct ServingCandidate
{
  uint32_t docId = 0;
  size_t shardOrdinal = 0;
  uint64_t matchedTermBits = 0;
  uint32_t bodyTf = 0;
  uint32_t titleTf = 0;
  uint32_t anchorTf = 0;
  double score = 0.0;
};

struct ServingPooledHit
{
  string url;
  string title;
  string description;
  double score = 0.0;
  size_t shardOrdinal = 0;
  uint32_t docId = 0;
};

struct ServingRankMetadata
{
  string url;
  string title;
  string description;
};

struct RankerStrategySet
{
  explicit RankerStrategySet(const vector<string>& queryTerms)
      : queryOccurrenceStrategy(queryTerms), exactTitleMatchStrategy(queryTerms)
  {
  }

  ExactPhraseMatch exactPhraseStrategy;
  CorrectOrdering correctOrderingStrategy;
  EarlyQueryTermMatch earlyQueryStrategy;
  TermCoverage termCoverageStrategy;
  QueryOccurrence queryOccurrenceStrategy;
  TitleTermCoverage titleTermCoverageStrategy;
  ExactTitleMatch exactTitleMatchStrategy;
  SeedDomainDepth seedDomainDepthStrategy;
  ShortTitleStrat shortTitleStrategy;
  ShortURLStrat shortUrlStrategy;
  DomainStrat domainStrategy;
  UrlDepth urlDepthStrategy;
  UrlPathDepth urlPathDepthStrategy;
  QueryParamPenalty queryParamPenaltyStrategy;

  void Attach(Ranker& ranker, const RankerWeights& w)
  {
    ranker.AddStrategy(&exactPhraseStrategy, w.exact_phrase);
    ranker.AddStrategy(&correctOrderingStrategy, w.correct_ordering);
    ranker.AddStrategy(&earlyQueryStrategy, w.early_query_term);
    ranker.AddStrategy(&termCoverageStrategy, w.term_coverage);
    ranker.AddStrategy(&queryOccurrenceStrategy, w.query_occurrence);
    ranker.AddStrategy(&shortTitleStrategy, w.short_title);
    ranker.AddStrategy(&shortUrlStrategy, w.short_url);
    ranker.AddStrategy(&domainStrategy, w.domain);
    ranker.AddStrategy(&urlDepthStrategy, w.url_depth);
    ranker.AddStrategy(&urlPathDepthStrategy, w.url_path_depth);
    ranker.AddStrategy(&titleTermCoverageStrategy, w.title_term_coverage);
    ranker.AddStrategy(&seedDomainDepthStrategy, w.seed_domain_depth);
    ranker.AddStrategy(&queryParamPenaltyStrategy, w.query_param_penalty);
    ranker.AddStrategy(&exactTitleMatchStrategy, w.exact_title_match);
  }
};

string ExtractDomain(const string& url)
{
  size_t hostStart = 0;
  const size_t schemeEnd = url.find("://");
  if (schemeEnd != string::npos)
    hostStart = schemeEnd + 3;

  size_t hostEnd = hostStart;
  while (hostEnd < url.size())
  {
    const char c = url[hostEnd];
    if (c == '/' || c == '?' || c == '#')
      break;
    ++hostEnd;
  }

  string domain;
  for (size_t i = hostStart; i < hostEnd; ++i)
    domain.pushBack(url[i]);
  return domain;
}

void InitializeMatchPositions(size_t matchCount, size_t termCount,
                              vector<MatchPositions>& matchPositions)
{
  matchPositions.clear();
  for (size_t i = 0; i < matchCount; ++i)
  {
    MatchPositions positions;
    for (size_t t = 0; t < termCount; ++t)
      positions.termPositions.pushBack(vector<size_t>());
    matchPositions.pushBack(positions);
  }
}

void CollectPositionsForTerms(const constraint_solver::IsrSource& source,
                              const vector<constraint_solver::DocumentMatch>& matches,
                              const vector<string>& terms, const string& prefix,
                              vector<MatchPositions>& matchPositions)
{
  for (size_t t = 0; t < terms.size(); ++t)
  {
    string lookup = prefix;
    lookup += terms[t];

    ISRWord* isr = source.OpenTermIsr(lookup);
    if (!isr)
      continue;

    size_t matchIdx = 0;
    for (Post* p = isr->Seek(1); p && matchIdx < matches.size(); p = isr->Next())
    {
      const Attributes attrs = p->GetAttributes();
      if (attrs.kind != Attributes::Kind::WORD)
        continue;

      const uint32_t docId = attrs.Word.documentId;
      while (matchIdx < matches.size() && matches[matchIdx].DocId < docId)
        ++matchIdx;

      if (matchIdx >= matches.size())
        break;

      if (matches[matchIdx].DocId == docId)
        matchPositions[matchIdx].termPositions[t].pushBack(p->GetStartLocation());
    }

    delete isr;
  }
}

void CollectQueryTerms(const constraint_solver::QueryNode* node, vector<string>& terms)
{
  if (!node)
    return;

  using constraint_solver::QueryNodeKind;
  switch (node->Kind)
  {
  case QueryNodeKind::Term:
    terms.pushBack(node->Value);
    break;
  case QueryNodeKind::Phrase:
    for (const string& t : node->PhraseTerms)
      terms.pushBack(t);
    break;
  case QueryNodeKind::Conjunction:
    for (const constraint_solver::QueryNode* child : node->RequiredChildren)
      CollectQueryTerms(child, terms);
    break;
  case QueryNodeKind::Or:
    for (const constraint_solver::QueryNode* child : node->Children)
      CollectQueryTerms(child, terms);
    break;
  default:
    break;
  }
}

uint64_t ServingTermBit(size_t index)
{
  return index < 64u ? (1ULL << index) : 0ULL;
}

double ServingScore(uint32_t bodyTf, uint32_t titleTf, uint32_t anchorTf)
{
  return static_cast<double>(bodyTf) + (3.0 * static_cast<double>(titleTf)) +
         (2.0 * static_cast<double>(anchorTf));
}

double ServingScoreFromQ16(uint64_t scoreQ16)
{
  return static_cast<double>(scoreQ16) / 65536.0;
}

size_t FindQueryTermIndex(const vector<string>& terms, const string& term)
{
  for (size_t i = 0; i < terms.size(); ++i)
  {
    if (terms[i] == term)
      return i;
  }
  return static_cast<size_t>(-1);
}

bool QueryRequiresPositions(const constraint_solver::QueryNode* node)
{
  if (!node)
    return false;

  using constraint_solver::QueryNodeKind;
  switch (node->Kind)
  {
  case QueryNodeKind::Phrase:
    return true;
  case QueryNodeKind::Conjunction:
    for (const constraint_solver::QueryNode* child : node->RequiredChildren)
    {
      if (QueryRequiresPositions(child))
        return true;
    }
    for (const constraint_solver::QueryNode* child : node->ExcludedChildren)
    {
      if (QueryRequiresPositions(child))
        return true;
    }
    return false;
  case QueryNodeKind::Or:
    for (const constraint_solver::QueryNode* child : node->Children)
    {
      if (QueryRequiresPositions(child))
        return true;
    }
    return false;
  default:
    return false;
  }
}

void InitializeSingleMatchPositions(size_t termCount, MatchPositions& positions)
{
  positions.termPositions.clear();
  for (size_t t = 0; t < termCount; ++t)
    positions.termPositions.pushBack(vector<size_t>());
}

void CollectServingCandidatePositions(const ServingShardSource& shard, uint32_t docId,
                                      const vector<string>& queryTerms, MatchPositions& body,
                                      MatchPositions& title, MatchPositions& anchor)
{
  InitializeSingleMatchPositions(queryTerms.size(), body);
  InitializeSingleMatchPositions(queryTerms.size(), title);
  InitializeSingleMatchPositions(queryTerms.size(), anchor);

  for (size_t termIndex = 0; termIndex < queryTerms.size(); ++termIndex)
  {
    shard.DecodeTermPositions(queryTerms[termIndex], docId, ServingPositionField::Body,
                              &body.termPositions[termIndex]);
    shard.DecodeTermPositions(queryTerms[termIndex], docId, ServingPositionField::Title,
                              &title.termPositions[termIndex]);
    shard.DecodeTermPositions(queryTerms[termIndex], docId, ServingPositionField::Anchor,
                              &anchor.termPositions[termIndex]);
  }
}

bool PositionsContainPhrase(const vector<string>& queryTerms, const vector<string>& phraseTerms,
                            const MatchPositions& fieldPositions)
{
  vector<vector<size_t>> phrasePositions;
  for (const string& term : phraseTerms)
  {
    const size_t index = FindQueryTermIndex(queryTerms, term);
    if (index == static_cast<size_t>(-1) || index >= fieldPositions.termPositions.size())
      return false;
    phrasePositions.pushBack(fieldPositions.termPositions[index]);
  }
  return ranking_strategy_detail::HasExactPhrase(phrasePositions);
}

ServingCandidate* FindOrAddServingCandidate(vector<ServingCandidate>& candidates,
                                            vector<uint32_t>& candidateIndexByDoc,
                                            uint32_t docId, size_t shardIndex)
{
  static constexpr uint32_t kInvalidCandidateIndex = 0xffffffffu;
  if (docId >= candidateIndexByDoc.size())
    return nullptr;

  const uint32_t existing = candidateIndexByDoc[docId];
  if (existing != kInvalidCandidateIndex && existing < candidates.size())
    return &candidates[existing];

  ServingCandidate fresh;
  fresh.docId = docId;
  fresh.shardOrdinal = shardIndex;
  candidates.pushBack(fresh);
  candidateIndexByDoc[docId] = static_cast<uint32_t>(candidates.size() - 1u);
  return &candidates.back();
}

void AddServingPosting(vector<ServingCandidate>& candidates, vector<uint32_t>& candidateIndexByDoc,
                       const ServingDecodedPosting& posting, size_t shardIndex,
                       size_t termIndex)
{
  ServingCandidate* candidate =
      FindOrAddServingCandidate(candidates, candidateIndexByDoc, posting.docId, shardIndex);
  if (!candidate)
    return;

  candidate->matchedTermBits |= ServingTermBit(termIndex);
  candidate->bodyTf += posting.bodyTf;
  candidate->titleTf += posting.titleTf;
  candidate->anchorTf += posting.anchorTf;
  candidate->score = ServingScore(candidate->bodyTf, candidate->titleTf, candidate->anchorTf);
}

bool ServingCandidateMatchesNode(const ServingCandidate& candidate, const vector<string>& terms,
                                 const constraint_solver::QueryNode* node);

double CurrentServingThreshold(const vector<ServingCandidate>& candidates,
                               const vector<string>& terms,
                               const constraint_solver::QueryNode* root, size_t candidateLimit)
{
  if (candidateLimit == 0 || candidates.size() < candidateLimit)
    return 0.0;

  vector<double> topScores;
  topScores.reserve(candidateLimit);
  for (size_t i = 0; i < candidates.size(); ++i)
  {
    const ServingCandidate& candidate = candidates[i];
    if (candidate.score <= 0.0 || !ServingCandidateMatchesNode(candidate, terms, root))
      continue;

    size_t pos = 0;
    while (pos < topScores.size() && topScores[pos] > candidate.score)
      ++pos;

    if (pos >= candidateLimit)
      continue;

    topScores.insert(pos, candidate.score);
    if (topScores.size() > candidateLimit)
      topScores.popBack();
  }

  return topScores.size() >= candidateLimit ? topScores.back() : 0.0;
}

bool ServingCandidateMatchesNode(const ServingCandidate& candidate, const vector<string>& terms,
                                 const constraint_solver::QueryNode* node)
{
  if (!node)
    return true;

  using constraint_solver::QueryNodeKind;
  switch (node->Kind)
  {
  case QueryNodeKind::Term:
  {
    const size_t index = FindQueryTermIndex(terms, node->Value);
    const uint64_t bit = ServingTermBit(index);
    return bit != 0 && (candidate.matchedTermBits & bit) != 0;
  }
  case QueryNodeKind::Phrase:
    for (const string& term : node->PhraseTerms)
    {
      const size_t index = FindQueryTermIndex(terms, term);
      const uint64_t bit = ServingTermBit(index);
      if (bit == 0 || (candidate.matchedTermBits & bit) == 0)
        return false;
    }
    return true;
  case QueryNodeKind::Conjunction:
    for (const constraint_solver::QueryNode* child : node->RequiredChildren)
    {
      if (!ServingCandidateMatchesNode(candidate, terms, child))
        return false;
    }
    for (const constraint_solver::QueryNode* child : node->ExcludedChildren)
    {
      if (ServingCandidateMatchesNode(candidate, terms, child))
        return false;
    }
    return true;
  case QueryNodeKind::Or:
    for (const constraint_solver::QueryNode* child : node->Children)
    {
      if (ServingCandidateMatchesNode(candidate, terms, child))
        return true;
    }
    return false;
  default:
    return false;
  }
}

bool ServingCandidateMatchesNodeExact(const ServingCandidate& candidate, const vector<string>& terms,
                                      const constraint_solver::QueryNode* node,
                                      const MatchPositions& bodyPositions,
                                      const MatchPositions& titlePositions,
                                      const MatchPositions& anchorPositions)
{
  if (!node)
    return true;

  using constraint_solver::QueryNodeKind;
  switch (node->Kind)
  {
  case QueryNodeKind::Term:
  {
    const size_t index = FindQueryTermIndex(terms, node->Value);
    const uint64_t bit = ServingTermBit(index);
    return bit != 0 && (candidate.matchedTermBits & bit) != 0;
  }
  case QueryNodeKind::Phrase:
    return PositionsContainPhrase(terms, node->PhraseTerms, bodyPositions) ||
           PositionsContainPhrase(terms, node->PhraseTerms, titlePositions) ||
           PositionsContainPhrase(terms, node->PhraseTerms, anchorPositions);
  case QueryNodeKind::Conjunction:
    for (const constraint_solver::QueryNode* child : node->RequiredChildren)
    {
      if (!ServingCandidateMatchesNodeExact(candidate, terms, child, bodyPositions,
                                           titlePositions, anchorPositions))
        return false;
    }
    for (const constraint_solver::QueryNode* child : node->ExcludedChildren)
    {
      if (ServingCandidateMatchesNodeExact(candidate, terms, child, bodyPositions,
                                          titlePositions, anchorPositions))
        return false;
    }
    return true;
  case QueryNodeKind::Or:
    for (const constraint_solver::QueryNode* child : node->Children)
    {
      if (ServingCandidateMatchesNodeExact(candidate, terms, child, bodyPositions,
                                          titlePositions, anchorPositions))
        return true;
    }
    return false;
  default:
    return false;
  }
}

string FindServingDescription(const vector<ServingRankMetadata>& metadata, const string& url)
{
  for (const ServingRankMetadata& entry : metadata)
  {
    if (entry.url == url)
      return entry.description;
  }
  return string();
}

SearchResponse SearchServingPage(const vector<ServingShardSource*>& shards,
                                 const constraint_solver::ConstraintSolver& solver,
                                 const string& query, size_t page, size_t limit)
{
  SearchResponse response = {};
  response.Query = query;
  response.Page = page == 0 ? 1 : page;
  response.Limit = NormalizeResultLimit(limit);

  if (IsBlankText(query))
    return response;

  const auto startedAt = std::chrono::steady_clock::now();
  const auto parseStartedAt = std::chrono::steady_clock::now();
  constraint_solver::ParseResult parsed = solver.ParseQuery(query);
  if (!parsed.Errors.empty())
  {
    for (const auto& error : parsed.Errors)
      response.Errors.pushBack({error.Offset, error.Message});
    constraint_solver::DestroyQueryTree(parsed.Root);
    response.ParseMs = DurationToMs(std::chrono::steady_clock::now() - parseStartedAt);
    response.TookMs = DurationToMs(std::chrono::steady_clock::now() - startedAt);
    return response;
  }

  vector<string> queryTerms;
  CollectQueryTerms(parsed.Root, queryTerms);
  response.ParseMs = DurationToMs(std::chrono::steady_clock::now() - parseStartedAt);

  const bool needsPositions = QueryRequiresPositions(parsed.Root);
  if (needsPositions)
  {
    for (const ServingShardSource* shard : shards)
    {
      if (shard && shard->IsLoaded() && !shard->HasPositionData())
      {
        response.Errors.pushBack(
            {0, string("phrase queries require rebuilt v2 .ssi shards with positions")});
        constraint_solver::DestroyQueryTree(parsed.Root);
        response.TookMs = DurationToMs(std::chrono::steady_clock::now() - startedAt);
        return response;
      }
    }
  }

  const auto retrieveStartedAt = std::chrono::steady_clock::now();
  const size_t candidateLimit = ComputeOverfetchLimit(response.Page, response.Limit);
  const RankerWeights weights = LoadRankerWeights("ranker_weights.conf");
  Ranker ranker(candidateLimit);
  RankerStrategySet strategies(queryTerms);
  strategies.Attach(ranker, weights);
  vector<ServingRankMetadata> rankMetadata;
  for (size_t shardIndex = 0; shardIndex < shards.size(); ++shardIndex)
  {
    const ServingShardSource* shard = shards[shardIndex];
    if (!shard || !shard->IsLoaded())
      continue;

    vector<ServingCandidate> candidates;
    vector<uint32_t> candidateIndexByDoc(shard->DocumentCount(), 0xffffffffu);
    vector<ServingTermView> termViews;
    vector<uint32_t> termMaxScoresQ16;
    termViews.reserve(queryTerms.size());
    termMaxScoresQ16.reserve(queryTerms.size());

    uint64_t totalTermMaxScoreQ16 = 0;
    for (size_t termIndex = 0; termIndex < queryTerms.size(); ++termIndex)
    {
      ServingTermView view;
      uint32_t maxScoreQ16 = 0;
      if (shard->LookupTerm(queryTerms[termIndex], &view) && view.IsValid())
        maxScoreQ16 = view.termInfo->maxScoreQ16;
      termViews.pushBack(view);
      termMaxScoresQ16.pushBack(maxScoreQ16);
      totalTermMaxScoreQ16 += maxScoreQ16;
    }

    for (uint32_t wantedTier = 0; wantedTier < kServingTierCount; ++wantedTier)
    {
      for (size_t termIndex = 0; termIndex < termViews.size(); ++termIndex)
      {
        const ServingTermView& view = termViews[termIndex];
        if (!view.IsValid())
          continue;

        for (uint32_t ti = 0; ti < view.tierCount; ++ti)
        {
          const ServingTierInfoV1& tier = view.tiers[ti];
          if (tier.tierId != wantedTier)
            continue;

          for (uint32_t bi = 0; bi < tier.blockCount; ++bi)
          {
            const ServingBlockInfoV1* block = shard->GetBlockInfo(tier, bi);
            if (!block)
              continue;

            const double threshold =
                CurrentServingThreshold(candidates, queryTerms, parsed.Root, candidateLimit);
            const uint64_t otherTermMaxQ16 =
                totalTermMaxScoreQ16 >= termMaxScoresQ16[termIndex]
                    ? totalTermMaxScoreQ16 - termMaxScoresQ16[termIndex]
                    : 0u;
            const double blockUpperBound =
                ServingScoreFromQ16(static_cast<uint64_t>(block->maxScoreQ16) + otherTermMaxQ16);
            if (threshold > 0.0 && blockUpperBound <= threshold)
              continue;

            ServingPostingBlockCursor cursor;
            if (!shard->OpenBlockCursor(*block, tier.tierId, &cursor))
              continue;

            ServingDecodedPosting posting;
            while (cursor.Next(&posting))
              AddServingPosting(candidates, candidateIndexByDoc, posting, shardIndex, termIndex);
          }
        }
      }
    }

    for (size_t i = 0; i < candidates.size(); ++i)
    {
      ServingCandidate& candidate = candidates[i];
      if (!ServingCandidateMatchesNode(candidate, queryTerms, parsed.Root))
        continue;

      const string url = shard->GetDocumentURL(candidate.docId);
      if (url.size() == 0)
        continue;

      if (candidate.score <= 0.0)
        continue;

      MatchPositions bodyPositions;
      MatchPositions titlePositions;
      MatchPositions anchorPositions;
      CollectServingCandidatePositions(*shard, candidate.docId, queryTerms, bodyPositions,
                                       titlePositions, anchorPositions);
      if (!shard->HasPositionData())
      {
        for (size_t termIndex = 0; termIndex < queryTerms.size(); ++termIndex)
        {
          const uint64_t bit = ServingTermBit(termIndex);
          if (bit != 0 && (candidate.matchedTermBits & bit) != 0)
            bodyPositions.termPositions[termIndex].pushBack(termIndex + 1u);
        }
      }
      if (needsPositions &&
          !ServingCandidateMatchesNodeExact(candidate, queryTerms, parsed.Root, bodyPositions,
                                           titlePositions, anchorPositions))
      {
        continue;
      }

      const string title = shard->GetDocumentTitle(candidate.docId);
      ServingDocRankMetadataV1 docRank = shard->GetDocumentRankMetadata(candidate.docId);

      urlEntry entry = {};
      entry.url = url;
      entry.domain = ExtractDomain(url);
      entry.depth = static_cast<int>(docRank.depth);
      entry.seedDomainDepth = static_cast<int>(docRank.domainDepth);

      Ranker::RankSignals signals = {};
      signals.title_word_count = docRank.titleWordCount;
      signals.title_term_positions = &titlePositions.termPositions;
      signals.anchor_term_positions = &anchorPositions.termPositions;

      ranker.Consider(entry, title, bodyPositions.termPositions, signals, shardIndex);
      rankMetadata.pushBack({url, title, shard->GetDocumentDescription(candidate.docId)});
    }
  }
  response.SolveMs = DurationToMs(std::chrono::steady_clock::now() - retrieveStartedAt);

  const auto rankStartedAt = std::chrono::steady_clock::now();
  const vector<Ranker::RankedResult> ranked = ranker.TopResults();

  static constexpr uint8_t kMaxResultsPerDomain = 5;
  unordered_map<uint8_t> seenUrls;
  unordered_map<uint8_t> domainCounts;
  const size_t pageStart = ComputeWindowStart(response.Page, response.Limit);
  const size_t pageEnd = pageStart > static_cast<size_t>(-1) - response.Limit
                             ? static_cast<size_t>(-1)
                             : pageStart + response.Limit;

  size_t uniqueIndex = 0;
  for (const Ranker::RankedResult& hit : ranked)
  {
    const uint8_t* seen = seenUrls.get(string_slice(hit.entry.url.cstr()));
    if (seen && *seen != 0)
      continue;

    const string domain = ExtractRegisteredDomain(hit.entry.url);
    const uint8_t* dc = domainCounts.get(string_slice(domain.cstr()));
    const uint8_t count = dc ? *dc : 0;
    if (count >= kMaxResultsPerDomain)
      continue;

    seenUrls.insert(hit.entry.url, 1u);
    domainCounts.insert(domain, count + 1u);
    if (uniqueIndex >= pageStart && uniqueIndex < pageEnd)
    {
      response.Results.pushBack({hit.entry.url, hit.title,
                                 FindServingDescription(rankMetadata, hit.entry.url), hit.score});
    }
    ++uniqueIndex;
    if (uniqueIndex >= kMaxVisibleSearchResults)
      break;
  }
  response.TotalHits = uniqueIndex > kMaxVisibleSearchResults ? kMaxVisibleSearchResults
                                                              : uniqueIndex;
  response.RankMs = DurationToMs(std::chrono::steady_clock::now() - rankStartedAt);
  response.TookMs = DurationToMs(std::chrono::steady_clock::now() - startedAt);

  constraint_solver::DestroyQueryTree(parsed.Root);
  return response;
}

unordered_map<MatchPositions> BuildAnchorPositionMap(
    const vector<IndexShardSource*>& anchorShards, const vector<string>& queryTerms)
{
  unordered_map<MatchPositions> anchorByUrl;

  for (size_t si = 0; si < anchorShards.size(); ++si)
  {
    const IndexShardSource* shard = anchorShards[si];
    if (!shard)
      continue;

    // Positions from different anchor shards use incompatible absolute coordinate
    // spaces, so each URL's positions must come from exactly one shard (the first
    // that claims it). Track which URLs were claimed by the current shard so later
    // shards can skip them without mixing coordinates.
    unordered_map<uint8_t> claimedThisShard;

    for (size_t t = 0; t < queryTerms.size(); ++t)
    {
      ISRWord* isr = shard->OpenTermIsr(queryTerms[t]);
      if (!isr)
        continue;

      uint32_t lastDocId = UINT32_MAX;
      MatchPositions* cachedMp = nullptr;

      for (Post* p = isr->Seek(1); p; p = isr->Next())
      {
        const Attributes attrs = p->GetAttributes();
        if (attrs.kind != Attributes::Kind::WORD)
          continue;

        const uint32_t docId = attrs.Word.documentId;
        if (docId != lastDocId)
        {
          lastDocId = docId;
          cachedMp = nullptr;

          const string url = shard->GetDocumentURL(docId);
          if (url.size() == 0)
            continue;

          MatchPositions* existing = anchorByUrl.get(string_slice(url.cstr()));
          if (existing)
          {
            // Only append to an entry claimed in the current shard — entries from
            // prior shards are in a different coordinate space.
            if (claimedThisShard.get(string_slice(url.cstr())))
              cachedMp = existing;
          }
          else
          {
            MatchPositions fresh;
            for (size_t ti = 0; ti < queryTerms.size(); ++ti)
              fresh.termPositions.pushBack(vector<size_t>());
            anchorByUrl.insert(url, fresh);
            cachedMp = anchorByUrl.get(string_slice(url.cstr()));
            claimedThisShard.insert(url, 1u);
          }
        }

        if (cachedMp)
          cachedMp->termPositions[t].pushBack(p->GetStartLocation());
      }

      delete isr;
    }
  }

  return anchorByUrl;
}

void CollectAllMatchPositions(ShardQueryContext& context, const vector<string>& terms,
                              const unordered_map<MatchPositions>* anchorByUrl)
{
  InitializeMatchPositions(context.solved.Matches.size(), terms.size(), context.bodyPositions);
  InitializeMatchPositions(context.solved.Matches.size(), terms.size(), context.titlePositions);
  InitializeMatchPositions(context.solved.Matches.size(), terms.size(), context.anchorPositions);

  CollectPositionsForTerms(context.querySource, context.solved.Matches, terms, string(),
                           context.bodyPositions);
  CollectPositionsForTerms(context.querySource, context.solved.Matches, terms, string("#"),
                           context.titlePositions);

  if (anchorByUrl)
  {
    for (size_t matchIdx = 0; matchIdx < context.solved.Matches.size(); ++matchIdx)
    {
      const string& url = context.solved.Matches[matchIdx].Url;
      const MatchPositions* ap = anchorByUrl->get(string_slice(url.cstr()));
      if (ap)
        context.anchorPositions[matchIdx] = *ap;
    }
  }
}

size_t DurationToMs(std::chrono::steady_clock::duration duration)
{
  return static_cast<size_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(duration).count());
}

bool QueryExceededDeadline(SteadyTimePoint deadline)
{
  return std::chrono::steady_clock::now() >= deadline;
}

string FindDescriptionForRankedHit(const vector<ShardQueryContext>& contexts,
                                   const vector<size_t>& matchedIndices,
                                   const Ranker::RankedResult& ranked)
{
  for (size_t i = 0; i < matchedIndices.size(); ++i)
  {
    const ShardQueryContext& context = contexts[matchedIndices[i]];
    if (context.shardOrdinal != ranked.shardOrdinal || context.shard == nullptr)
      continue;

    for (size_t matchIndex = 0; matchIndex < context.solved.Matches.size(); ++matchIndex)
    {
      const auto& match = context.solved.Matches[matchIndex];
      if (match.Url == ranked.entry.url)
        return context.shard->GetDocumentDescription(match.DocId);
    }
  }

  return string();
}

void MarkTimeout(SearchResponse& response, size_t thresholdMs)
{
  response.TotalHits = 0;
  response.Results.clear();
  response.Errors.clear();

  string message("query timed out after exceeding ");
  search::AppendUnsigned(message, thresholdMs);
  message += string(" ms threshold");
  response.Errors.pushBack({0, message});
}

size_t ComputeWindowStart(size_t page, size_t limit)
{
  if (page <= 1 || limit == 0)
  {
    return 0;
  }

  const size_t maxSize = static_cast<size_t>(-1);
  if (page - 1 > maxSize / limit)
  {
    return maxSize;
  }

  return (page - 1) * limit;
}

size_t NormalizeResultLimit(size_t limit)
{
  return limit == 0 ? kDefaultSearchLimit : limit;
}

size_t OnlineCpuCount()
{
  const long cpuCount = sysconf(_SC_NPROCESSORS_ONLN);
  return cpuCount > 0 ? static_cast<size_t>(cpuCount) : 1u;
}

size_t ParsePositiveSizeT(const char* text)
{
  if (!text || *text == '\0')
    return 0;

  char* end = nullptr;
  const unsigned long long value = std::strtoull(text, &end, 10);
  if (!end || *end != '\0')
    return 0;
  return static_cast<size_t>(value);
}

size_t ResolveShardWorkerCount(size_t shardCount)
{
  if (shardCount <= 1)
    return shardCount == 0 ? 1u : shardCount;

  const char* configured = std::getenv("SEARCH_SHARD_WORKERS");
  const size_t requested = ParsePositiveSizeT(configured);
  if (requested > 0)
    return requested > shardCount ? shardCount : requested;

  size_t autoCount = OnlineCpuCount();
  if (autoCount > 8u)
    autoCount = 8u;
  if (autoCount == 0)
    autoCount = 1u;
  return autoCount > shardCount ? shardCount : autoCount;
}

bool ResolveConfiguredDurationMs(const char* envName, size_t multiplier, size_t* outMs)
{
  const char* configured = std::getenv(envName);
  if (!configured || *configured == '\0' || !outMs)
    return false;

  const size_t parsed = ParsePositiveSizeT(configured);
  *outMs = parsed * multiplier;
  return true;
}

bool ResolveShardLoadTimeoutMs(size_t* outMs)
{
  if (ResolveConfiguredDurationMs("SEARCH_LOAD_TIMEOUT_MS", 1u, outMs))
    return true;
  return ResolveConfiguredDurationMs("SEARCH_LOAD_TIMEOUT_SEC", 1000u, outMs);
}

struct LoadProgressState
{
  size_t totalPaths = 0;
  bool enforceDeadline = false;
  SteadyTimePoint deadline = {};
  atomic_counter processedPaths;
  atomic_counter loadedPrimaryShards;
  atomic_counter loadedAnchorShards;
  atomic_counter failedShardLoads;
  atomic_counter cacheHits;
  atomic_counter cacheRebuilds;
  atomic_counter cacheWriteFailures;
  atomic_counter timedOut;
  atomic_counter loadingComplete;
};

void LogLoadProgress(const LoadProgressState& progress, size_t completed)
{
  const size_t percent = progress.totalPaths == 0 ? 100u : (completed * 100u) / progress.totalPaths;
  std::fprintf(stderr,
               "[search] shard load progress: %zu/%zu (%zu%%) primary=%d anchor=%d failed=%d "
               "cache_hit=%d cache_rebuilt=%d cache_write_failed=%d%s\n",
               completed, progress.totalPaths, percent, progress.loadedPrimaryShards.get_val(),
               progress.loadedAnchorShards.get_val(), progress.failedShardLoads.get_val(),
               progress.cacheHits.get_val(), progress.cacheRebuilds.get_val(),
               progress.cacheWriteFailures.get_val(),
               progress.timedOut.load() ? " cutoff-reached" : "");
}

void RecordShardCacheStatus(LoadProgressState* progress, const IndexShardSource& shard)
{
  if (!progress)
    return;

  switch (shard.GetCacheStatus())
  {
  case ShardLoadCacheStatus::Hit:
    progress->cacheHits.fetch_add(1);
    break;
  case ShardLoadCacheStatus::Rebuilt:
    progress->cacheRebuilds.fetch_add(1);
    break;
  case ShardLoadCacheStatus::RebuiltWithoutWrite:
    progress->cacheRebuilds.fetch_add(1);
    progress->cacheWriteFailures.fetch_add(1);
    break;
  case ShardLoadCacheStatus::NotLoaded:
    break;
  }
}

void* RunLoadProgressReporter(void* rawArgs)
{
  LoadProgressState* progress = static_cast<LoadProgressState*>(rawArgs);
  if (!progress)
    return nullptr;

  size_t lastLogged = static_cast<size_t>(-1);
  while (!progress->loadingComplete.load())
  {
    const size_t completed = static_cast<size_t>(progress->processedPaths.get_val());
    if (completed != lastLogged)
    {
      LogLoadProgress(*progress, completed);
      lastLogged = completed;
    }
    usleep(1000 * 1000);
  }

  const size_t completed = static_cast<size_t>(progress->processedPaths.get_val());
  if (completed != lastLogged || progress->timedOut.load())
    LogLoadProgress(*progress, completed);
  return nullptr;
}

size_t ClampWorkerCount(size_t desired, size_t itemCount)
{
  if (itemCount == 0)
    return 0;
  if (desired == 0)
    return 1;
  return desired > itemCount ? itemCount : desired;
}

size_t RangeBegin(size_t workerIndex, size_t workerCount, size_t itemCount)
{
  return (itemCount * workerIndex) / workerCount;
}

size_t RangeEnd(size_t workerIndex, size_t workerCount, size_t itemCount)
{
  return (itemCount * (workerIndex + 1u)) / workerCount;
}

struct SolvePhaseArgs
{
  const constraint_solver::ConstraintSolver* solver = nullptr;
  const constraint_solver::QueryNode* root = nullptr;
  vector<ShardQueryContext>* contexts = nullptr;
  size_t begin = 0;
  size_t end = 0;
  SteadyTimePoint deadline = {};
  atomic_counter* timedOut = nullptr;
};

void* RunSolvePhase(void* rawArgs)
{
  SolvePhaseArgs* args = static_cast<SolvePhaseArgs*>(rawArgs);
  if (!args || !args->solver || !args->contexts)
    return nullptr;

  for (size_t i = args->begin; i < args->end && !args->timedOut->load(); ++i)
  {
    if ((i & 63) == 0 && QueryExceededDeadline(args->deadline))
    {
      args->timedOut->store(1);
      return nullptr;
    }

    ShardQueryContext& context = (*args->contexts)[i];
    context.solved =
        args->solver->SolveParsedUntil(args->root, context.querySource, args->deadline);
    if (context.solved.TimedOut)
    {
      args->timedOut->store(1);
    }
  }

  return nullptr;
}

struct CollectPhaseArgs
{
  vector<ShardQueryContext>* contexts = nullptr;
  const vector<size_t>* matchedIndices = nullptr;
  const vector<string>* queryTerms = nullptr;
  const unordered_map<MatchPositions>* anchorByUrl = nullptr;
  size_t begin = 0;
  size_t end = 0;
  SteadyTimePoint deadline = {};
  atomic_counter* timedOut = nullptr;
};

void* RunCollectPhase(void* rawArgs)
{
  CollectPhaseArgs* args = static_cast<CollectPhaseArgs*>(rawArgs);
  if (!args || !args->contexts || !args->matchedIndices || !args->queryTerms)
    return nullptr;

  for (size_t i = args->begin; i < args->end && !args->timedOut->load(); ++i)
  {
    if ((i & 63) == 0 && QueryExceededDeadline(args->deadline))
    {
      args->timedOut->store(1);
      return nullptr;
    }

    ShardQueryContext& context = (*args->contexts)[(*args->matchedIndices)[i]];
    CollectAllMatchPositions(context, *args->queryTerms, args->anchorByUrl);
  }

  return nullptr;
}

struct RankPhaseArgs
{
  vector<ShardQueryContext>* contexts = nullptr;
  const vector<size_t>* matchedIndices = nullptr;
  const vector<string>* queryTerms = nullptr;
  size_t begin = 0;
  size_t end = 0;
  size_t candidateLimit = 0;
  SteadyTimePoint deadline = {};
  atomic_counter* timedOut = nullptr;
  vector<PooledRankedHit>* output = nullptr;
  const RankerWeights* weights = nullptr;
};

struct LoadShardArgs
{
  const vector<string>* paths = nullptr;
  size_t begin = 0;
  size_t end = 0;
  vector<IndexShardSource*> shards;
  vector<IndexShardSource*> anchorShards;
  LoadProgressState* progress = nullptr;
};

void* RunLoadShardRange(void* rawArgs)
{
  LoadShardArgs* args = static_cast<LoadShardArgs*>(rawArgs);
  if (!args || !args->paths)
    return nullptr;

  for (size_t i = args->begin; i < args->end; ++i)
  {
    if (args->progress && args->progress->timedOut.load())
      return nullptr;

    if (args->progress && args->progress->enforceDeadline &&
        std::chrono::steady_clock::now() >= args->progress->deadline)
    {
      args->progress->timedOut.store(1);
      return nullptr;
    }

    const string& path = (*args->paths)[i];
    IndexShardSource* shard = new IndexShardSource(path);
    if (!shard->IsLoaded())
    {
      delete shard;
      if (args->progress)
        args->progress->failedShardLoads.fetch_add(1);
    }
    else if (IsAnchorShard(path))
    {
      RecordShardCacheStatus(args->progress, *shard);
      args->anchorShards.pushBack(shard);
      if (args->progress)
        args->progress->loadedAnchorShards.fetch_add(1);
    }
    else
    {
      RecordShardCacheStatus(args->progress, *shard);
      args->shards.pushBack(shard);
      if (args->progress)
        args->progress->loadedPrimaryShards.fetch_add(1);
    }

    if (args->progress)
      args->progress->processedPaths.fetch_add(1);
  }

  return nullptr;
}

void* RunRankPhase(void* rawArgs)
{
  RankPhaseArgs* args = static_cast<RankPhaseArgs*>(rawArgs);
  if (!args || !args->contexts || !args->matchedIndices || !args->queryTerms || !args->output)
    return nullptr;

  const RankerWeights defaultWeights;
  const RankerWeights& w = args->weights ? *args->weights : defaultWeights;

  Ranker ranker(args->candidateLimit);
  ExactPhraseMatch exactPhraseStrategy;
  CorrectOrdering correctOrderingStrategy;
  EarlyQueryTermMatch earlyQueryStrategy;
  TermCoverage termCoverageStrategy;
  QueryOccurrence queryOccurrenceStrategy(*args->queryTerms);
  TitleTermCoverage titleTermCoverageStrategy;
  ExactTitleMatch exactTitleMatchStrategy(*args->queryTerms);
  SeedDomainDepth seedDomainDepthStrategy;
  ShortTitleStrat shortTitleStrategy;
  ShortURLStrat shortUrlStrategy;
  DomainStrat domainStrategy;
  UrlDepth urlDepthStrategy;
  UrlPathDepth urlPathDepthStrategy;
  QueryParamPenalty queryParamPenaltyStrategy;

  // Strategies are always registered in the ranker_weights.conf fixed order
  ranker.AddStrategy(&exactPhraseStrategy, w.exact_phrase);
  ranker.AddStrategy(&correctOrderingStrategy, w.correct_ordering);
  ranker.AddStrategy(&earlyQueryStrategy, w.early_query_term);
  ranker.AddStrategy(&termCoverageStrategy, w.term_coverage);
  ranker.AddStrategy(&queryOccurrenceStrategy, w.query_occurrence);
  ranker.AddStrategy(&shortTitleStrategy, w.short_title);
  ranker.AddStrategy(&shortUrlStrategy, w.short_url);
  ranker.AddStrategy(&domainStrategy, w.domain);
  ranker.AddStrategy(&urlDepthStrategy, w.url_depth);
  ranker.AddStrategy(&urlPathDepthStrategy, w.url_path_depth);
  ranker.AddStrategy(&titleTermCoverageStrategy, w.title_term_coverage);
  ranker.AddStrategy(&seedDomainDepthStrategy, w.seed_domain_depth);
  ranker.AddStrategy(&queryParamPenaltyStrategy, w.query_param_penalty);
  ranker.AddStrategy(&exactTitleMatchStrategy, w.exact_title_match);

  auto flushPartialResults = [&]()
  {
    const vector<Ranker::RankedResult> topResults = ranker.TopResults();
    args->output->clear();
    for (size_t ri = 0; ri < topResults.size(); ++ri)
    {
      args->output->pushBack({topResults[ri], topResults[ri].shardOrdinal,
                              FindDescriptionForRankedHit(*args->contexts, *args->matchedIndices,
                                                          topResults[ri])});
    }
  };

  for (size_t i = args->begin; i < args->end && !args->timedOut->load(); ++i)
  {
    if ((i & 63) == 0 && QueryExceededDeadline(args->deadline))
    {
      args->timedOut->store(1);
      flushPartialResults();
      return nullptr;
    }

    ShardQueryContext& context = (*args->contexts)[(*args->matchedIndices)[i]];
    if (context.bodyPositions.size() != context.solved.Matches.size())
      continue;

    for (size_t matchIndex = 0; matchIndex < context.solved.Matches.size(); ++matchIndex)
    {
      if ((matchIndex & 63) == 0 && QueryExceededDeadline(args->deadline))
      {
        args->timedOut->store(1);
        flushPartialResults();
        return nullptr;
      }

      const auto& match = context.solved.Matches[matchIndex];
      const DocumentInfo* docInfo = context.shard->GetDocumentInfo(match.DocId);

      urlEntry entry = {};
      entry.url = match.Url;
      entry.domain = ExtractDomain(match.Url);

      Ranker::RankSignals signals = {};
      if (docInfo)
      {
        entry.depth = static_cast<int>(docInfo->rankingInput.depth);
        entry.seedDomainDepth = static_cast<int>(docInfo->rankingInput.domainDepth);
        signals.title_word_count = docInfo->rankingInput.titleWordCount;
      }
      if (matchIndex < context.titlePositions.size())
        signals.title_term_positions = &context.titlePositions[matchIndex].termPositions;
      if (matchIndex < context.anchorPositions.size())
        signals.anchor_term_positions = &context.anchorPositions[matchIndex].termPositions;

      ranker.Consider(entry, context.shard->GetDocumentTitle(match.DocId),
                      context.bodyPositions[matchIndex].termPositions, signals,
                      context.shardOrdinal);
    }
  }

  const vector<Ranker::RankedResult> topResults = ranker.TopResults();
  args->output->clear();
  for (size_t i = 0; i < topResults.size(); ++i)
  {
    args->output->pushBack({topResults[i], topResults[i].shardOrdinal,
                            FindDescriptionForRankedHit(*args->contexts, *args->matchedIndices,
                                                        topResults[i])});
  }

  return nullptr;
}

void LaunchSolvePhase(vector<ShardQueryContext>& contexts,
                      const constraint_solver::ConstraintSolver& solver,
                      const constraint_solver::QueryNode* root, size_t workerCount,
                      SteadyTimePoint deadline, atomic_counter& timedOut)
{
  const size_t phaseWorkers = ClampWorkerCount(workerCount, contexts.size());
  if (phaseWorkers == 0)
    return;

  thread* workers = phaseWorkers > 1 ? new thread[phaseWorkers - 1] : nullptr;
  SolvePhaseArgs* args = phaseWorkers > 1 ? new SolvePhaseArgs[phaseWorkers - 1] : nullptr;

  for (size_t workerIndex = 1; workerIndex < phaseWorkers; ++workerIndex)
  {
    args[workerIndex - 1] = {
        .solver = &solver,
        .root = root,
        .contexts = &contexts,
        .begin = RangeBegin(workerIndex, phaseWorkers, contexts.size()),
        .end = RangeEnd(workerIndex, phaseWorkers, contexts.size()),
        .deadline = deadline,
        .timedOut = &timedOut,
    };
    workers[workerIndex - 1].start(RunSolvePhase, &args[workerIndex - 1]);
  }

  SolvePhaseArgs current = {
      .solver = &solver,
      .root = root,
      .contexts = &contexts,
      .begin = 0,
      .end = RangeEnd(0, phaseWorkers, contexts.size()),
      .deadline = deadline,
      .timedOut = &timedOut,
  };
  RunSolvePhase(&current);

  for (size_t i = 0; i + 1 < phaseWorkers; ++i)
    workers[i].join();

  delete[] args;
  delete[] workers;
}

void LaunchCollectPhase(vector<ShardQueryContext>& contexts, const vector<size_t>& matchedIndices,
                        const vector<string>& queryTerms,
                        const unordered_map<MatchPositions>* anchorByUrl, size_t workerCount,
                        SteadyTimePoint deadline, atomic_counter& timedOut)
{
  const size_t phaseWorkers = ClampWorkerCount(workerCount, matchedIndices.size());
  if (phaseWorkers == 0)
    return;

  thread* workers = phaseWorkers > 1 ? new thread[phaseWorkers - 1] : nullptr;
  CollectPhaseArgs* args = phaseWorkers > 1 ? new CollectPhaseArgs[phaseWorkers - 1] : nullptr;

  for (size_t workerIndex = 1; workerIndex < phaseWorkers; ++workerIndex)
  {
    args[workerIndex - 1] = {
        .contexts = &contexts,
        .matchedIndices = &matchedIndices,
        .queryTerms = &queryTerms,
        .anchorByUrl = anchorByUrl,
        .begin = RangeBegin(workerIndex, phaseWorkers, matchedIndices.size()),
        .end = RangeEnd(workerIndex, phaseWorkers, matchedIndices.size()),
        .deadline = deadline,
        .timedOut = &timedOut,
    };
    workers[workerIndex - 1].start(RunCollectPhase, &args[workerIndex - 1]);
  }

  CollectPhaseArgs current = {
      .contexts = &contexts,
      .matchedIndices = &matchedIndices,
      .queryTerms = &queryTerms,
      .anchorByUrl = anchorByUrl,
      .begin = 0,
      .end = RangeEnd(0, phaseWorkers, matchedIndices.size()),
      .deadline = deadline,
      .timedOut = &timedOut,
  };
  RunCollectPhase(&current);

  for (size_t i = 0; i + 1 < phaseWorkers; ++i)
    workers[i].join();

  delete[] args;
  delete[] workers;
}

void LaunchRankPhase(vector<ShardQueryContext>& contexts, const vector<size_t>& matchedIndices,
                     const vector<string>& queryTerms, size_t workerCount, size_t candidateLimit,
                     SteadyTimePoint deadline, atomic_counter& timedOut,
                     vector<vector<PooledRankedHit>>& workerOutputs, const RankerWeights& weights)
{
  const size_t phaseWorkers = ClampWorkerCount(workerCount, matchedIndices.size());
  workerOutputs.clear();
  workerOutputs.resize(phaseWorkers);
  if (phaseWorkers == 0)
    return;

  thread* workers = phaseWorkers > 1 ? new thread[phaseWorkers - 1] : nullptr;
  RankPhaseArgs* args = phaseWorkers > 1 ? new RankPhaseArgs[phaseWorkers - 1] : nullptr;

  for (size_t workerIndex = 1; workerIndex < phaseWorkers; ++workerIndex)
  {
    args[workerIndex - 1] = {
        .contexts = &contexts,
        .matchedIndices = &matchedIndices,
        .queryTerms = &queryTerms,
        .begin = RangeBegin(workerIndex, phaseWorkers, matchedIndices.size()),
        .end = RangeEnd(workerIndex, phaseWorkers, matchedIndices.size()),
        .candidateLimit = candidateLimit,
        .deadline = deadline,
        .timedOut = &timedOut,
        .output = &workerOutputs[workerIndex],
        .weights = &weights,
    };
    workers[workerIndex - 1].start(RunRankPhase, &args[workerIndex - 1]);
  }

  RankPhaseArgs current = {
      .contexts = &contexts,
      .matchedIndices = &matchedIndices,
      .queryTerms = &queryTerms,
      .begin = 0,
      .end = RangeEnd(0, phaseWorkers, matchedIndices.size()),
      .candidateLimit = candidateLimit,
      .deadline = deadline,
      .timedOut = &timedOut,
      .output = &workerOutputs[0],
      .weights = &weights,
  };
  RunRankPhase(&current);

  for (size_t i = 0; i + 1 < phaseWorkers; ++i)
    workers[i].join();

  delete[] args;
  delete[] workers;
}

} // namespace

SearchService::SearchService(const string& indexDirectory, bool loadShards)
    : indexDirectory_(indexDirectory)
{
  if (loadShards)
    LoadShards();
}

SearchService::~SearchService()
{
  for (IndexShardSource* shard : shards_)
    delete shard;
  for (IndexShardSource* shard : anchorShards_)
    delete shard;
  for (ServingShardSource* shard : servingShards_)
    delete shard;
}

void SearchService::SetQueryExecutionTimeThresholdMs(size_t thresholdMs)
{
  solver_.SetExecutionTimeThresholdMs(thresholdMs);
}

size_t SearchService::GetQueryExecutionTimeThresholdMs() const
{
  return solver_.GetExecutionTimeThresholdMs();
}

vector<string> SearchService::DiscoverShardPaths() const
{
  return search::DiscoverShardPaths(indexDirectory_);
}

bool SearchService::IsAnchorShardPath(const string& path)
{
  return IsAnchorShard(path);
}

bool SearchService::IsServingShardPath(const string& path)
{
  return HasSsiSuffix(path);
}

void SearchService::ResetLoadedShards()
{
  for (IndexShardSource* shard : shards_)
    delete shard;
  for (IndexShardSource* shard : anchorShards_)
    delete shard;
  for (ServingShardSource* shard : servingShards_)
    delete shard;
  shards_.clear();
  anchorShards_.clear();
  servingShards_.clear();
}

void SearchService::AppendLoadedShard(IndexShardSource* shard, bool isAnchorShard)
{
  if (!shard)
    return;

  if (isAnchorShard)
    anchorShards_.pushBack(shard);
  else
    shards_.pushBack(shard);
}

void SearchService::AppendLoadedServingShard(ServingShardSource* shard)
{
  if (shard)
    servingShards_.pushBack(shard);
}

size_t SearchService::PrimaryShardCount() const
{
  if (!servingShards_.empty())
    return servingShards_.size();
  return shards_.size();
}

size_t SearchService::AnchorShardCount() const
{
  return anchorShards_.size();
}

LoadShardsResult SearchService::LoadShards()
{
  const auto startedAt = std::chrono::steady_clock::now();
  ResetLoadedShards();

  LoadShardsResult result = {};
  const vector<string> paths = ::avengine::search::DiscoverShardPaths(indexDirectory_);
  result.TotalShardPaths = paths.size();
  const size_t loadWorkers = ResolveShardWorkerCount(paths.size());
  if (loadWorkers == 0)
  {
    result.TookMs = DurationToMs(std::chrono::steady_clock::now() - startedAt);
    return result;
  }

  size_t loadTimeoutMs = 0;
  const bool enforceDeadline = ResolveShardLoadTimeoutMs(&loadTimeoutMs);
  if (paths.size() != 0)
  {
    if (enforceDeadline)
    {
      std::fprintf(stderr,
                   "[search] loading %zu shard path(s) from %s using %zu worker(s) with cutoff %zu ms\n",
                   paths.size(), indexDirectory_.cstr(), loadWorkers, loadTimeoutMs);
    }
    else
    {
      std::fprintf(stderr, "[search] loading %zu shard path(s) from %s using %zu worker(s)\n",
                   paths.size(), indexDirectory_.cstr(), loadWorkers);
    }
  }

  if (paths.size() != 0 && IsServingShardPath(paths[0]))
  {
    for (size_t i = 0; i < paths.size(); ++i)
    {
      if (enforceDeadline && std::chrono::steady_clock::now() >=
                                 startedAt + std::chrono::milliseconds(loadTimeoutMs))
      {
        result.TimedOut = true;
        break;
      }

      ServingShardSource* shard = new ServingShardSource(paths[i]);
      if (!shard->IsLoaded())
      {
        delete shard;
        ++result.FailedShardLoads;
        continue;
      }

      servingShards_.pushBack(shard);
      ++result.LoadedPrimaryShards;
    }

    result.TookMs = DurationToMs(std::chrono::steady_clock::now() - startedAt);
    return result;
  }

  LoadProgressState progress = {
      .totalPaths = paths.size(),
      .enforceDeadline = enforceDeadline,
      .deadline = enforceDeadline
                      ? startedAt + std::chrono::milliseconds(loadTimeoutMs)
                      : SteadyTimePoint(),
  };

  LoadShardArgs* args = new LoadShardArgs[loadWorkers];
  thread* workers = loadWorkers > 1 ? new thread[loadWorkers - 1] : nullptr;
  thread progressReporter;
  const bool reporterStarted =
      paths.size() != 0 && progressReporter.start(RunLoadProgressReporter, &progress);

  for (size_t workerIndex = 1; workerIndex < loadWorkers; ++workerIndex)
  {
    args[workerIndex].paths = &paths;
    args[workerIndex].begin = RangeBegin(workerIndex, loadWorkers, paths.size());
    args[workerIndex].end = RangeEnd(workerIndex, loadWorkers, paths.size());
    args[workerIndex].progress = &progress;
    if (!workers[workerIndex - 1].start(RunLoadShardRange, &args[workerIndex]))
      RunLoadShardRange(&args[workerIndex]);
  }

  args[0].paths = &paths;
  args[0].begin = 0;
  args[0].end = RangeEnd(0, loadWorkers, paths.size());
  args[0].progress = &progress;
  RunLoadShardRange(&args[0]);

  for (size_t i = 0; i + 1 < loadWorkers; ++i)
  {
    if (workers[i].joinable())
      workers[i].join();
  }

  progress.loadingComplete.store(1);
  if (reporterStarted && progressReporter.joinable())
    progressReporter.join();

  for (size_t workerIndex = 0; workerIndex < loadWorkers; ++workerIndex)
  {
    for (size_t i = 0; i < args[workerIndex].shards.size(); ++i)
      shards_.pushBack(args[workerIndex].shards[i]);
    for (size_t i = 0; i < args[workerIndex].anchorShards.size(); ++i)
      anchorShards_.pushBack(args[workerIndex].anchorShards[i]);
  }

  delete[] workers;
  delete[] args;

  result.LoadedPrimaryShards = static_cast<size_t>(progress.loadedPrimaryShards.get_val());
  result.LoadedAnchorShards = static_cast<size_t>(progress.loadedAnchorShards.get_val());
  result.FailedShardLoads = static_cast<size_t>(progress.failedShardLoads.get_val());
  result.TimedOut = progress.timedOut.load();
  result.TookMs = DurationToMs(std::chrono::steady_clock::now() - startedAt);

  return result;
}

SearchResponse SearchService::Search(const string& query, size_t limit) const
{
  return SearchPage(query, 1, limit);
}

SearchResponse SearchService::SearchPage(const string& query, size_t page, size_t limit) const
{
  SearchResponse response = {};
  response.Query = query;
  response.Page = page == 0 ? 1 : page;
  response.Limit = NormalizeResultLimit(limit);

  if (IsBlankText(query))
    return response;

  if (!servingShards_.empty())
    return SearchServingPage(servingShards_, solver_, query, response.Page, response.Limit);

  const RankerWeights weights = LoadRankerWeights("ranker_weights.conf");

  const auto startedAt = std::chrono::steady_clock::now();
  const size_t thresholdMs = solver_.GetExecutionTimeThresholdMs();
  const SteadyTimePoint deadline = startedAt + std::chrono::milliseconds(thresholdMs);
  const size_t candidateLimit = ComputeOverfetchLimit(response.Page, response.Limit);

  const auto parseStartedAt = std::chrono::steady_clock::now();
  constraint_solver::ParseResult parsed = solver_.ParseQuery(query);
  if (!parsed.Errors.empty())
  {
    for (const auto& error : parsed.Errors)
      response.Errors.pushBack({error.Offset, error.Message});
    constraint_solver::DestroyQueryTree(parsed.Root);
    response.ParseMs = DurationToMs(std::chrono::steady_clock::now() - parseStartedAt);
    response.TookMs = DurationToMs(std::chrono::steady_clock::now() - startedAt);
    return response;
  }

  vector<string> queryTerms;
  CollectQueryTerms(parsed.Root, queryTerms);
  response.ParseMs = DurationToMs(std::chrono::steady_clock::now() - parseStartedAt);

  if (QueryExceededDeadline(deadline))
  {
    constraint_solver::DestroyQueryTree(parsed.Root);
    MarkTimeout(response, thresholdMs);
    response.TookMs = DurationToMs(std::chrono::steady_clock::now() - startedAt);
    return response;
  }

  vector<ShardQueryContext> contexts;
  contexts.reserve(shards_.size());
  for (size_t i = 0; i < shards_.size(); ++i)
    contexts.emplaceBack(shards_[i], i);

  const size_t shardWorkerCount = ResolveShardWorkerCount(shards_.size());
  atomic_counter timedOut(0);

  const auto solveStartedAt = std::chrono::steady_clock::now();
  LaunchSolvePhase(contexts, solver_, parsed.Root, shardWorkerCount, deadline, timedOut);
  response.SolveMs = DurationToMs(std::chrono::steady_clock::now() - solveStartedAt);

  const bool solveTimedOut = timedOut.load();
  timedOut.store(0);

  // If solve timed out give rest of pipelineive a
  // new window so they can finish processing the partial results
  const SteadyTimePoint postSolveDeadline =
      solveTimedOut
          ? std::chrono::steady_clock::now() + std::chrono::milliseconds(thresholdMs)
          : deadline;

  vector<size_t> matchedIndices;
  matchedIndices.reserve(contexts.size());
  for (size_t i = 0; i < contexts.size(); ++i)
  {
    if (!contexts[i].solved.Matches.empty())
      matchedIndices.pushBack(i);
    response.TotalHits += contexts[i].solved.Matches.size();
  }

  const unordered_map<MatchPositions> anchorByUrl =
      (!matchedIndices.empty() && !anchorShards_.empty())
          ? BuildAnchorPositionMap(anchorShards_, queryTerms)
          : unordered_map<MatchPositions>();

  const auto collectStartedAt = std::chrono::steady_clock::now();
  LaunchCollectPhase(contexts, matchedIndices, queryTerms, &anchorByUrl, shardWorkerCount,
                     postSolveDeadline, timedOut);
  response.CollectMs = DurationToMs(std::chrono::steady_clock::now() - collectStartedAt);

  const bool collectTimedOut = timedOut.load();
  timedOut.store(0);

  const SteadyTimePoint postCollectDeadline =
      collectTimedOut
          ? std::chrono::steady_clock::now() + std::chrono::milliseconds(thresholdMs)
          : postSolveDeadline;

  const auto rankStartedAt = std::chrono::steady_clock::now();
  vector<vector<PooledRankedHit>> workerOutputs;
  LaunchRankPhase(contexts, matchedIndices, queryTerms, shardWorkerCount, candidateLimit,
                  postCollectDeadline, timedOut, workerOutputs, weights);
  response.RankMs = DurationToMs(std::chrono::steady_clock::now() - rankStartedAt);

  vector<PooledRankedHit> pooledHits;
  for (size_t workerIndex = 0; workerIndex < workerOutputs.size(); ++workerIndex)
  {
    for (size_t i = 0; i < workerOutputs[workerIndex].size(); ++i)
      pooledHits.pushBack(workerOutputs[workerIndex][i]);
  }

  pooledHits.sort(
      [](const PooledRankedHit& lhs, const PooledRankedHit& rhs)
      {
        if (lhs.ranked.score != rhs.ranked.score)
          return lhs.ranked.score > rhs.ranked.score;
        if (lhs.shardOrdinal != rhs.shardOrdinal)
          return lhs.shardOrdinal < rhs.shardOrdinal;
        if (lhs.ranked.insertionIndex != rhs.ranked.insertionIndex)
          return lhs.ranked.insertionIndex < rhs.ranked.insertionIndex;
        return lhs.ranked.entry.url < rhs.ranked.entry.url;
      });

  static constexpr uint8_t kMaxResultsPerDomain = 5;

  unordered_map<uint8_t> seenUrls;
  unordered_map<uint8_t> domainCounts;
  const size_t pageStart = ComputeWindowStart(response.Page, response.Limit);
  const size_t pageEnd = pageStart > static_cast<size_t>(-1) - response.Limit
                             ? static_cast<size_t>(-1)
                             : pageStart + response.Limit;

  size_t uniqueIndex = 0;
  for (size_t i = 0; i < pooledHits.size(); ++i)
  {
    const string& url = pooledHits[i].ranked.entry.url;
    const uint8_t* seen = seenUrls.get(string_slice(url.cstr()));
    if (seen && *seen != 0)
      continue;

    const string domain = ExtractRegisteredDomain(url);
    const uint8_t* dc = domainCounts.get(string_slice(domain.cstr()));
    const uint8_t count = dc ? *dc : 0;
    if (count >= kMaxResultsPerDomain)
      continue;

    seenUrls.insert(url, 1u);
    domainCounts.insert(domain, count + 1);
    if (uniqueIndex >= pageStart && uniqueIndex < pageEnd)
    {
      SearchHit hit;
      hit.Url = pooledHits[i].ranked.entry.url;
      hit.Title = pooledHits[i].ranked.title;
      hit.Description = pooledHits[i].description;
      hit.Score = pooledHits[i].ranked.score;
      response.Results.pushBack(hit);
    }
    ++uniqueIndex;
    if (uniqueIndex >= kMaxVisibleSearchResults)
      break;
  }
  if (uniqueIndex > kMaxVisibleSearchResults)
    response.TotalHits = kMaxVisibleSearchResults;
  else
    response.TotalHits = uniqueIndex;

  {
    // Rewrite score.log from scratch on each query
    FILE* log = fopen("score.log", "w");
    if (log != nullptr)
    {
      static const char* kNames[] = {
          "exact_phrase",       "correct_ordering", "early_query_term",   "term_coverage",
          "query_occurrence",   "short_title",      "short_url",          "domain",
          "url_depth",          "url_path_depth",   "title_term_coverage", "seed_domain_depth",
          "query_param_penalty",  "exact_title_match",
      };
      static const size_t kNamesLen = sizeof(kNames) / sizeof(kNames[0]);

      fprintf(log, "query: %s\n", query.cstr());
      fprintf(log, "timing: parse=%zums solve=%zums collect=%zums rank=%zums\n",
              response.ParseMs, response.SolveMs, response.CollectMs, response.RankMs);
      fprintf(log, "terms:");
      for (size_t t = 0; t < queryTerms.size(); ++t)
        fprintf(log, " [%s]", queryTerms[t].cstr());
      fprintf(log, "\n");
      unordered_map<uint8_t> logSeen;
      size_t rank = 0;
      for (size_t i = 0; i < pooledHits.size() && rank < kMaxVisibleSearchResults; ++i)
      {
        const string& url = pooledHits[i].ranked.entry.url;
        const uint8_t* seen = logSeen.get(string_slice(url.cstr()));
        if (seen && *seen != 0)
          continue;
        logSeen.insert(url, 1u);
        fprintf(log, "  #%zu total=%.4f %s\n", rank + 1, pooledHits[i].ranked.score, url.cstr());
        fprintf(log, "        title: %s\n", pooledHits[i].ranked.title.cstr());
        const vector<double>& ss = pooledHits[i].ranked.strategy_scores;
        for (size_t s = 0; s < ss.size() && s < kNamesLen; ++s)
          fprintf(log, "        %-22s %.4f\n", kNames[s], ss[s]);
        ++rank;
      }
      fclose(log);
    }
  }

  constraint_solver::DestroyQueryTree(parsed.Root);
  response.TookMs = DurationToMs(std::chrono::steady_clock::now() - startedAt);
  return response;
}

} // namespace search
} // namespace avengine
