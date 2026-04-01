#pragma once

#include <avengine/avengine_lib/string.h>
#include <avengine/avengine_lib/string_slice.h>
#include <avengine/unordered_map.h>
#include <cstdint>
namespace avengine
{
namespace crawler
{

struct UrlDocInfo
{
  uint32_t docid = 0;
  uint32_t shard_id = 0;
  bool indexed = false;
};

class UrlDocMap
{
public:
  bool Contains(string_slice canonical_url) const;
  const UrlDocInfo* Find(string_slice canonical_url) const;
  UrlDocInfo* FindMutable(string_slice canonical_url);

  UrlDocInfo& Upsert(const string& canonical_url, const UrlDocInfo& info);
  bool MarkIndexed(string_slice canonical_url, bool indexed = true);

  size_t Size() const;

  bool WriteSnapshot(const string& path) const;
  bool LoadSnapshot(const string& path);

private:
  unordered_map<UrlDocInfo> entries_;
}

} // namespace crawler
} // namespace avengine
