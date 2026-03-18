#include <avengine/crawler/frontier.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

namespace
{
void fail(const char* expr, int line)
{
  fprintf(stderr, "crawler_frontier_test failed at line %d: %s\n", line, expr);
  abort();
}

#define EXPECT(expr)                                                                               \
  do                                                                                               \
  {                                                                                                \
    if (!(expr))                                                                                   \
      fail(#expr, __LINE__);                                                                       \
  } while (false)

bool strEq(const avengine::string& lhs, const char* rhs)
{
  return strcmp(lhs.cstr(), rhs) == 0;
}

bool hasEntry(const avengine::vector<avengine::urlEntry>& batch, const char* url, int depth,
              const char* domain)
{
  for (size_t i = 0; i < batch.size(); ++i)
  {
    const avengine::urlEntry& e = batch[i];
    if (strEq(e.url, url) && e.depth == depth && strEq(e.domain, domain))
    {
      return true;
    }
  }
  return false;
}
}

int main()
{
  // Empty frontier and zero batch-size edge cases.
  
  avengine::crawler::frontier empty;
  avengine::vector<avengine::urlEntry> b = empty.nextBatch(10, 5);
  EXPECT(b.size() == 0);
  EXPECT(empty.size() == 0);

  empty.add("https://zero.example/a", 1);
  avengine::vector<avengine::urlEntry> zeroBatch = empty.nextBatch(10, 0);
  EXPECT(zeroBatch.size() == 0);
  EXPECT(empty.size() == 1);
  
  avengine::crawler::frontier f;

  avengine::vector<avengine::string> seeds;
  seeds.pushBack("https://a.com/start");
  seeds.pushBack("https://a.com/start");
  seeds.pushBack("https://b.com/high");
  f.seed(seeds);

  // Dupe check
  EXPECT(f.size() == 2);
  // Check bloomfilter
  EXPECT(f.alreadySeen("https://a.com/start"));
  EXPECT(f.alreadySeen("https://b.com/high"));

  // add with different depth
  f.add("https://c.com/mid", 2);
  f.add("https://b.com/high", 9); // duplicate should be ignored
  EXPECT(f.size() == 3);

  // Test get batch
  avengine::vector<avengine::urlEntry> batch = f.nextBatch(100, 2);
  EXPECT(batch.size() == 2);
  EXPECT(batch[0].depth <= batch[1].depth);
  EXPECT(batch[0].depth == 0);
  EXPECT(batch[1].depth == 0);
  EXPECT(strEq(batch[0].url, "https://a.com/start") || strEq(batch[1].url, "https://a.com/start"));
  EXPECT(strEq(batch[0].url, "https://b.com/high") || strEq(batch[1].url, "https://b.com/high"));
  EXPECT(f.size() == 1);

  // Ensure it only grabs one final url
  avengine::vector<avengine::urlEntry> finalBatch = f.nextBatch(100, 10);
  EXPECT(finalBatch.size() == 1);
  EXPECT(strEq(finalBatch[0].url, "https://c.com/mid"));
  EXPECT(f.size() == 0);

  // check seen count
  avengine::crawler::frontier dup;
  dup.add("https://dup.example/a", 1);
  EXPECT(dup.size() == 1);
  EXPECT(dup.seenCount() == 1);
  dup.add("https://dup.example/a", 2);
  EXPECT(dup.size() == 1);
  EXPECT(dup.seenCount() == 1);
  

  // Force random sampling (sampleSize < size) and ensure all URLs are returned once
  avengine::crawler::frontier sampled;
  sampled.add("https://rand.example/u1", 1);
  sampled.add("https://rand.example/u2", 2);
  sampled.add("https://rand.example/u3", 3);
  sampled.add("https://rand.example/u4", 4);
  sampled.add("https://rand.example/u5", 5);
  sampled.add("https://rand.example/u6", 6);

  bool got1 = false;
  bool got2 = false;
  bool got3 = false;
  bool got4 = false;
  bool got5 = false;
  bool got6 = false;

  int iterations = 0;
  while (sampled.size() > 0)
  {
    avengine::vector<avengine::urlEntry> one = sampled.nextBatch(2, 1);
    EXPECT(one.size() == 1);

    if (strEq(one[0].url, "https://rand.example/u1")) 
    {
      got1 = true;
    }
    else if (strEq(one[0].url, "https://rand.example/u2")) 
    {
      got2 = true;
    }
    else if (strEq(one[0].url, "https://rand.example/u3")) 
    {
      got3 = true;
    }
    else if (strEq(one[0].url, "https://rand.example/u4"))
    {
      got4 = true;
    }
    else if (strEq(one[0].url, "https://rand.example/u5"))
    {
      got5 = true;
    }
    else if (strEq(one[0].url, "https://rand.example/u6"))
    {
      got6 = true;
    }
    else
    {
      EXPECT(false);
    }

    ++iterations;
    EXPECT(iterations <= 6);
  }

  EXPECT(got1 && got2 && got3 && got4 && got5 && got6);


  // Checkpoint file save error
  avengine::crawler::frontier err;
  avengine::expected<void, avengine::error> saveBad =
                        err.saveCheckpoint("/this/path/does/not/exist/frontier.bin");
  EXPECT(!saveBad.hasValue());

  // Checkpoint file load error
  avengine::expected<void, avengine::error> loadBad =
      err.loadCheckpoint("/this/path/does/not/exist/frontier.bin");
  EXPECT(!loadBad.hasValue());
  

  // Test checkpointing
  avengine::crawler::frontier original;
  original.add("https://a.com/start", 1);
  original.add("https://b.com/start", 2);

  // create temp file for checkpointing
  char pathTemplate[] = "/tmp/frontier_checkpoint_XXXXXX";
  int fd = mkstemp(pathTemplate);
  EXPECT(fd >= 0);
  EXPECT(close(fd) == 0);

  // save
  avengine::expected<void, avengine::error> saveRes = original.saveCheckpoint(pathTemplate);
  EXPECT(saveRes.hasValue());

  // load
  avengine::crawler::frontier loaded;
  avengine::expected<void, avengine::error> loadRes = loaded.loadCheckpoint(pathTemplate);
  EXPECT(loadRes.hasValue());

  // check load size
  EXPECT(loaded.size() == 2);

  // check load batch
  avengine::vector<avengine::urlEntry> loadedBatch = loaded.nextBatch(100, 10);
  EXPECT(loadedBatch.size() == 2);
  EXPECT(loadedBatch[0].depth <= loadedBatch[1].depth);
  EXPECT(hasEntry(loadedBatch, "https://a.com/start", 1, "a.com"));
  EXPECT(hasEntry(loadedBatch, "https://b.com/start", 2, "b.com"));

  EXPECT(unlink(pathTemplate) == 0);
  return 0;
}
