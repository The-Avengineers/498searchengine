#pragma once

#include "avengine/avengine_lib/string.h"
#include <cctype>
#include <cstdint>

using RankingInput = uint64_t;

namespace ranking_input
{
namespace detail
{
constexpr uint64_t kDepthMask = 0xFFFFull;
constexpr uint64_t kUrlLengthMask = 0xFFFFFFull;
constexpr uint64_t kTitleWordCountMask = 0xFFFFFFull;
} // namespace detail

inline RankingInput Pack(uint32_t depth, uint32_t urlLength, uint32_t titleWordCount)
{
  const uint64_t packedDepth =
      static_cast<uint64_t>(depth > detail::kDepthMask ? detail::kDepthMask : depth);
  const uint64_t packedUrlLength = static_cast<uint64_t>(
      urlLength > detail::kUrlLengthMask ? detail::kUrlLengthMask : urlLength);
  const uint64_t packedTitleWordCount = static_cast<uint64_t>(
      titleWordCount > detail::kTitleWordCountMask ? detail::kTitleWordCountMask
                                                   : titleWordCount);
  return (packedDepth << 48u) | (packedUrlLength << 24u) | packedTitleWordCount;
}

inline uint32_t Depth(RankingInput value)
{
  return static_cast<uint32_t>(value >> 48u);
}

inline uint32_t UrlLength(RankingInput value)
{
  return static_cast<uint32_t>((value >> 24u) & detail::kUrlLengthMask);
}

inline uint32_t TitleWordCount(RankingInput value)
{
  return static_cast<uint32_t>(value & detail::kTitleWordCountMask);
}

inline uint32_t CountWords(const avengine::string& text)
{
  uint32_t count = 0;
  bool inWord = false;

  for (size_t i = 0; i < text.size(); ++i)
  {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    if (std::isalpha(c))
    {
      if (!inWord)
      {
        ++count;
        inWord = true;
      }
    }
    else
    {
      inWord = false;
    }
  }

  return count;
}

} // namespace ranking_input
