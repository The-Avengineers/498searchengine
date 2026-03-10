#include "../../include/avengine/crawler/frontier.h"
#include <cstdio>
#include <cstdlib>
#include <ctime>

using avengine::error;
using avengine::expected;
using avengine::string;

namespace logging_helpers
{

expected<void, error> write(const void* ptr, size_t size, FILE* f)
{
  if (fwrite(ptr, 1, size, f) != size)
  {
    fclose(f);
    return expected<void, error>::error(error("write failed"));
  }

  return expected<void, error>::ok();
}

template <typename T> expected<void, error> writeVal(T val, FILE* f)
{
  return write(&val, sizeof(val), f);
}

expected<void, error> writeString(const string& s, FILE* f)
{
  auto res = writeVal(s.size(), f);
  if (!res.hasValue())
  {
    return res;
  }

  return write(s.cstr(), s.size(), f);
}

expected<void, error> read(void* ptr, size_t size, FILE* f)
{
  if (fread(ptr, 1, size, f) != size)
  {
    fclose(f);
    return expected<void, error>::error(error("read failed"));
  }
  return expected<void, error>::ok();
}

template <typename T> expected<void, error> readVal(T& val, FILE* f)
{
  return read(&val, sizeof(val), f);
}

expected<void, error> readString(string& s, FILE* f)
{
  size_t len;
  auto res = readVal(len, f);
  if (!res.hasValue())
  {
    return res;
  }
  // Build string char by char
  s = "";
  char c;
  for (size_t i = 0; i < len; ++i)
  {
    res = read(&c, 1, f);
    if (!res.hasValue())
    {
      return res;
    }
    s.pushBack(c);
  }
  return expected<void, error>::ok();
}

} // namespace logging_helpers

namespace avengine
{
namespace crawler
{

frontier::frontier() : seen(1000000, .0001)
// 1M expected items, 0.01% FP rate
{
  srand(static_cast<unsigned>(time(nullptr)));
}

frontier::~frontier() {}

string frontier::extractDomain(const string& url) const
{
  // skip '://'
  size_t start = 0;
  for (size_t i = 0; i + 2 < url.size(); ++i)
  {
    if (url[i] == ':' && url[i + 1] == '/' && url[i + 2] == '/')
    {
      start = i + 3;
      break;
    }
  }

  // find end of the host
  size_t end = start;
  while (end < url.size() && url[end] != '/' && url[end] != ':')
    ++end;

  // Build lowercase domain
  string domain;
  for (size_t i = start; i < end; ++i)
  {
    char c = url[i];
    if (c >= 'A' && c <= 'Z')
    {
      c += 32;
    }
    domain.pushBack(c);
  }
  return domain;
}

bool frontier::alreadySeen(const string& url) const
{
  return seen.contains(url.cstr());
}

void frontier::markSeen(const string& url)
{
  seen.insert(url.cstr());
}

void frontier::seed(const vector<string>& seedURLs)
{
  for (size_t i = 0; i < seedURLs.size(); ++i)
  {
    add(seedURLs[i], 0);
  }
}

void frontier::add(const string& url, int depth)
{
  if (alreadySeen(url))
  {
    return;
  }

  markSeen(url);

  urlEntry entry;
  entry.url = url;
  entry.domain = extractDomain(url);
  entry.depth = depth;

  masterList.pushBack(entry);
}

vector<urlEntry> frontier::nextBatch(size_t sampleSize, size_t batchSize)
{
  vector<urlEntry> batch;

  if (masterList.size() == 0)
  {
    return batch;
  }

  // Ensure we arent wasting work
  vector<urlEntry> sample;
  if (masterList.size() <= sampleSize)
  {
    sample = masterList;
    masterList.clear();
  }
  else
  {
    // Pick sampleSize random URLs
    size_t end = masterList.size();
    for (size_t i = 0; i < sampleSize; ++i)
    {
      size_t j = rand() % end;
      --end;
      // swap to the end
      urlEntry tmp = masterList[j];
      masterList[j] = masterList[end];
      masterList[end] = tmp;
    }
    // Pop the sample off the back of the list and add to a new list
    for (size_t i = 0; i < sampleSize; ++i)
    {
      sample.pushBack(masterList[masterList.size() - 1]);
      masterList.popBack();
    }
  }

  // Insertion sort sample by depth in ascending order (closer to seed first)
  for (size_t i = 1; i < sample.size(); ++i)
  {
    urlEntry key = sample[i];
    size_t j = i;
    while (j > 0 && sample[j - 1].depth > key.depth)
    {
      sample[j] = sample[j - 1];
      --j;
    }
    sample[j] = key;
  }

  // Take top batchSize
  size_t actualBatchSize{};
  if (batchSize < sample.size())
  {
    actualBatchSize = batchSize;
  }
  else
  {
    actualBatchSize = sample.size();
  }
  for (size_t i = 0; i < actualBatchSize; ++i)
  {
    batch.pushBack(sample[i]);
  }

  // Put remaining back into master list
  for (size_t i = actualBatchSize; i < sample.size(); ++i)
  {
    masterList.pushBack(sample[i]);
  }

  return batch;
}

expected<void, error> frontier::saveCheckpoint(const string& path) const
{
  FILE* f = fopen(path.cstr(), "wb");
  if (!f)
  {
    return expected<void, error>::error(error("failed to open checkpoint file"));
  }

  // bloom filter
  auto res = logging_helpers::writeVal(seen.getSize(), f); // size
  if (!res.hasValue())
  {
    return res;
  }
  res = logging_helpers::write(seen.data(), seen.getSize(), f); // data
  if (!res.hasValue())
  {
    return res;
  }

  // master list size
  res = logging_helpers::writeVal(masterList.size(), f);
  if (!res.hasValue())
  {
    return res;
  }

  // write each entry
  for (size_t i = 0; i < masterList.size(); ++i)
  {
    const urlEntry& entry = masterList[i];

    res = logging_helpers::writeString(entry.url, f);
    if (!res.hasValue())
    {
      return res;
    }

    res = logging_helpers::writeString(entry.domain, f);
    if (!res.hasValue())
    {
      return res;
    }

    res = logging_helpers::writeVal(entry.depth, f);
    if (!res.hasValue())
    {
      return res;
    }
  }

  fclose(f);
  return expected<void, error>::ok();
}

expected<void, error> frontier::loadCheckpoint(const string& path)
{
  FILE* f = fopen(path.cstr(), "rb");
  if (!f)
  {
    return expected<void, error>::error(error("failed to open checkpoint file"));
  }

  // bloom filter
  uint64_t bloomSize;
  auto res = logging_helpers::readVal(bloomSize, f); // size
  if (!res.hasValue())
  {
    return res;
  }
  seen.resize(bloomSize);
  res = logging_helpers::read(seen.data(), seen.getSize(), f); // data
  if (!res.hasValue())
  {
    return res;
  }

  // master list
  size_t listSize;
  res = logging_helpers::readVal(listSize, f);
  if (!res.hasValue())
  {
    return res;
  }

  masterList.clear();
  for (size_t i = 0; i < listSize; ++i)
  {
    urlEntry entry;
    // get URL
    res = logging_helpers::readString(entry.url, f);
    if (!res.hasValue())
    {
      return res;
    }

    // get domain
    res = logging_helpers::readString(entry.domain, f);
    if (!res.hasValue())
    {
      return res;
    }

    // get the depth
    res = logging_helpers::readVal(entry.depth, f);
    if (!res.hasValue())
    {
      return res;
    }
    masterList.pushBack(entry);
  }

  fclose(f);
  return expected<void, error>::ok();
}

size_t frontier::size() const
{
  return masterList.size();
}

size_t frontier::seenCount() const
{
  return seen.getCount();
}

} // namespace crawler
} // namespace avengine
