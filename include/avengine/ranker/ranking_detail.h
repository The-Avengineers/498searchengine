#pragma once

#include <avengine/avengine_lib/vector.h>

#include <cmath>
#include <cstddef>

namespace ranking_strategy_detail
{

inline bool HasExactPhrase(const avengine::vector<avengine::vector<size_t>>& term_positions)
{
  if (term_positions.size() < 2)
    return !term_positions.empty() && !term_positions[0].empty();

  for (const auto& positions : term_positions)
  {
    if (positions.empty())
      return false;
  }

  avengine::vector<size_t> indices(term_positions.size(), 0);
  while (indices[0] < term_positions[0].size())
  {
    const size_t base = term_positions[0][indices[0]];
    bool is_phrase = true;

    for (size_t term_idx = 1; term_idx < term_positions.size(); ++term_idx)
    {
      const size_t target = base + term_idx;
      while (indices[term_idx] < term_positions[term_idx].size() &&
             term_positions[term_idx][indices[term_idx]] < target)
      {
        ++indices[term_idx];
      }

      if (indices[term_idx] >= term_positions[term_idx].size() ||
          term_positions[term_idx][indices[term_idx]] != target)
      {
        is_phrase = false;
        break;
      }
    }

    if (is_phrase)
      return true;

    ++indices[0];
  }

  return false;
}

inline double OrderingScore(const avengine::vector<avengine::vector<size_t>>& term_positions)
{
  const size_t query_len = term_positions.size();
  if (query_len == 0)
    return 0.0;
  if (query_len == 1)
    return term_positions[0].empty() ? 0.0 : 1.0;

  size_t best_len = 0;
  size_t best_span = 0;

  for (size_t start_term = 0; start_term < query_len; ++start_term)
  {
    const avengine::vector<size_t>& starts = term_positions[start_term];
    if (starts.empty())
      continue;

    for (size_t si = 0; si < starts.size(); ++si)
    {
      size_t start_pos = starts[si];
      size_t prev_pos = start_pos;
      size_t end_pos = start_pos;
      size_t len = 1;

      for (size_t term_idx = start_term + 1; term_idx < query_len; ++term_idx)
      {
        const avengine::vector<size_t>& positions = term_positions[term_idx];
        if (positions.empty())
          break;

        bool found = false;
        for (size_t pi = 0; pi < positions.size(); ++pi)
        {
          if (positions[pi] > prev_pos)
          {
            prev_pos = positions[pi];
            end_pos = prev_pos;
            ++len;
            found = true;
            break;
          }
        }

        if (!found)
          break;
      }

      const size_t span = end_pos - start_pos;
      if (len > best_len || (len == best_len && span < best_span))
      {
        best_len = len;
        best_span = span;
      }
    }
  }

  if (best_len < 2)
    return 0.0;

  const double length_ratio = static_cast<double>(best_len) / static_cast<double>(query_len);
  const size_t min_span = best_len - 1;
  const size_t slack = best_span > min_span ? (best_span - min_span) : 0;
  const double proximity = 1.0 / (1.0 + static_cast<double>(slack));
  return length_ratio * proximity;
}

inline double CoverageScore(const avengine::vector<avengine::vector<size_t>>& term_positions)
{
  const size_t query_len = term_positions.size();
  if (query_len == 0)
    return 0.0;

  size_t present = 0;
  for (const auto& positions : term_positions)
  {
    if (!positions.empty())
      ++present;
  }

  return static_cast<double>(present) / static_cast<double>(query_len);
}

inline double EarlyMatchScore(const avengine::vector<avengine::vector<size_t>>& term_positions)
{
  if (term_positions.empty())
    return 0.0;

  double score = 0.0;
  size_t valid_terms = 0;
  for (const auto& positions : term_positions)
  {
    if (!positions.empty())
    {
      score += 1.0 / log2(2.0 + (0.1 * static_cast<double>(positions[0])));
      ++valid_terms;
    }
  }

  return valid_terms == 0 ? 0.0 : (score / valid_terms);
}

} // namespace ranking_strategy_detail
