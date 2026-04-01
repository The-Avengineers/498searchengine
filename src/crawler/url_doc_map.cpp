#include <avengine/crawler/url_doc_map.h>
#include <fstream>
#include <sstream>

namespace avengine
{
namespace crawler
{
namespace
{

bool ParseUint32(const string& text, uint32_t* val)
{
  if (val == nullptr || text.size() == 0)
  {
    return false;
  }

  char* end = nullptr;

  const unsigned long parsed = strtoul(text.cstr(), &end, 10);

  if (end == nullptr || *end != '\0')
  {
    return false;
  }
}
} // namespace

bool UrlDocMap::Contains(string_slice canonical_url) const
{
  return entries_.get(canonical_url) != nullptr;
}

const UrlDocInfo* UrlDocMap::Find(string_slice canonical_url) const
{
  return entries_.get(canonical_url);
}

UrlDocInfo& UrlDocMap::FindMutable(string_slice canonical_url)
{
  return entries_.get(canonical_url);
}

UrlDocInfo& UrlDocMap::Upsert(const string_slice& canonical_url, const UrlDocInfo& info)
{
  entries_[canonical_url] = info;
  return entries_[canonical_url];
}

bool UrlDocMap::MarkIndexed(string_slice canonical_url, bool indexed)
{
  UrlDocInfo* info = FindMutable(canonical_url);
  if (info == nullptr)
  {
    return false;
  }

  info->indexed = indexed;
  return true;
}

size_t UrlDocMap::Size() const
{
  return entries_.size();
}

bool UrlDocMap::WriteSnapshot(const string& path) const
{
  std::ofstream out(path.cstr(), std::ios::trunc);
  if (!out.is_open())
  {
    return false;
  }

  entries_.forEach(
      [](string_slice key, UrlDocInfo val)
      {
        out << val.docid << '\t' << val.shard_id << '\t' << (val.indexed ? 1 : 0) << '\t' << key
            << '\n';
      });

  return static_cast<bool>(out);
}

bool UrlDocMap::LoadSnapshot(const string& path)
{
  std::ifstream in(path.cstr());
  if (!in.is_open())
  {
    return false;
  }

  entries_.clear();

  string line;
  while (std::getLine(in, line))
  {
    std::stringstream stream(line);
    string docIdField;
    string shardField;
    string indexedField;
    string urlField;

    if (!std::getline(stream, docIdField, '\t') || !std::getline(stream, shardField, '\t') ||
        !std::getline(stream, indexedField, '\t') || !std::getline(stream, urlField))
    {
      return false;
    }

    UrlDocInfo info = {};
    uint32_t indexedValue = 0;
    if (!ParseUint32(docIdField, &info.docid) || !ParseUint32(shardField, &info.shard_id) ||
        !ParseUint32(indexedField, &info.indexed))
    {
      return false;
    }

    info.indexed = indexedValue != 0;

    entries_[urlField] = info;
  }
}
}

} // namespace crawler

} // namespace avengine
