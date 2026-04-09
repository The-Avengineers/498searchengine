#include <avengine/search/search_service.h>
#include <avengine/string.h>
#include <avengine/vector.h>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>

// Prevent the compiler from eliminating the search work as dead code.
static volatile size_t g_sink = 0;

static void PrintUsage(const char* prog)
{
  std::fprintf(stderr,
               "Usage: %s --index-dir <dir> --queries <file>"
               " [--iterations N] [--warmup N]\n",
               prog);
}

int main(int argc, char** argv)
{
  const char* indexDir = nullptr;
  const char* queriesFile = nullptr;
  int iterations = 100;
  int warmup = 3;

  for (int i = 1; i < argc; ++i)
  {
    const avengine::string arg(argv[i]);
    if (arg == "--index-dir" && i + 1 < argc)
      indexDir = argv[++i];
    else if (arg == "--queries" && i + 1 < argc)
      queriesFile = argv[++i];
    else if (arg == "--iterations" && i + 1 < argc)
      iterations = std::atoi(argv[++i]);
    else if (arg == "--warmup" && i + 1 < argc)
      warmup = std::atoi(argv[++i]);
    else
    {
      PrintUsage(argv[0]);
      return 1;
    }
  }

  if (!indexDir || !queriesFile)
  {
    PrintUsage(argv[0]);
    return 1;
  }

  avengine::vector<avengine::string> queries;
  {
    std::ifstream ifs(queriesFile);
    if (!ifs)
    {
      std::fprintf(stderr, "Cannot open queries file: %s\n", queriesFile);
      return 1;
    }
    std::string line;
    while (std::getline(ifs, line))
    {
      if (!line.empty())
        queries.pushBack(avengine::string(line.c_str()));
    }
  }

  if (queries.empty())
  {
    std::fprintf(stderr, "No queries found in %s\n", queriesFile);
    return 1;
  }

  const avengine::string indexDirStr(indexDir);
  const avengine::search::SearchService service(indexDirStr);

  // Warmup passes — results discarded so the caches are hot before timing.
  for (int w = 0; w < warmup; ++w)
  {
    for (const avengine::string& q : queries)
    {
      const auto r = service.Search(q, 10);
      g_sink += r.TotalHits;
    }
  }

  struct Stats
  {
    avengine::string query;
    double meanMs = 0.0;
    double minMs = 0.0;
    double maxMs = 0.0;
    size_t totalHits = 0;
  };

  avengine::vector<Stats> results(queries.size());
  for (size_t qi = 0; qi < queries.size(); ++qi)
    results[qi].query = queries[qi];

  for (int iter = 0; iter < iterations; ++iter)
  {
    for (size_t qi = 0; qi < queries.size(); ++qi)
    {
      const auto t0 = std::chrono::steady_clock::now();
      const auto r = service.Search(queries[qi], 10);
      const double ms =
          std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();

      g_sink += r.TotalHits;
      Stats& s = results[qi];
      s.totalHits = r.TotalHits;
      s.meanMs += ms;
      if (iter == 0 || ms < s.minMs)
        s.minMs = ms;
      if (ms > s.maxMs)
        s.maxMs = ms;
    }
  }

  std::printf("%-40s  %6s  %8s  %8s  %8s\n", "query", "hits", "mean_ms", "min_ms", "max_ms");
  std::printf("%-40s  %6s  %8s  %8s  %8s\n", "-----", "----", "-------", "------", "------");
  for (const Stats& s : results)
  {
    std::printf("%-40s  %6zu  %8.3f  %8.3f  %8.3f\n", s.query.cstr(), s.totalHits,
                s.meanMs / static_cast<double>(iterations), s.minMs, s.maxMs);
  }

  return 0;
}
