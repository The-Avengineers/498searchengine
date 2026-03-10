#include "avengine/vector.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>

namespace
{
struct ProbeStats
{
  uint64_t inserts = 0;
  uint64_t totalProbes = 0;
  uint64_t maxProbes = 0;
};

// Small deterministic mixer so we do not benchmark only sequential keys.
static uint32_t permute(uint32_t x)
{
  x ^= x >> 16;
  x *= 0x7feb352dU;
  x ^= x >> 15;
  x *= 0x846ca68bU;
  x ^= x >> 16;
  return x;
}

static size_t djb2HashLocal(const char* str)
{
  size_t hash = 5381;
  int c;
  while ((c = *str++))
    hash = ((hash << 5) + hash) + c;
  return hash;
}

static size_t makeKeyHash(uint32_t id)
{
  char key[32];
  std::snprintf(key, sizeof(key), "term_%u", permute(id));
  return djb2HashLocal(key);
}

static ProbeStats benchmarkLinear(size_t tableSize, size_t termsToInsert)
{
  ProbeStats stats{};
  avengine::vector<uint8_t> occupied(tableSize);
  for (size_t i = 0; i < tableSize; i++)
    occupied[i] = 0;

  for (size_t i = 0; i < termsToInsert; i++)
  {
    size_t start = makeKeyHash(static_cast<uint32_t>(i)) % tableSize;
    size_t idx = start;
    uint64_t probes = 1;

    while (occupied[idx])
    {
      idx++;
      if (idx == tableSize)
        idx = 0;
      probes++;
    }

    occupied[idx] = 1;
    stats.inserts++;
    stats.totalProbes += probes;
    if (probes > stats.maxProbes)
      stats.maxProbes = probes;
  }

  return stats;
}

static ProbeStats benchmarkQuadratic(size_t tableSize, size_t termsToInsert)
{
  ProbeStats stats{};
  avengine::vector<uint8_t> occupied(tableSize);
  for (size_t i = 0; i < tableSize; i++)
    occupied[i] = 0;

  for (size_t i = 0; i < termsToInsert; i++)
  {
    size_t start = makeKeyHash(static_cast<uint32_t>(i)) % tableSize;
    uint64_t probes = 0;
    bool inserted = false;

    for (size_t step = 0; step < tableSize; step++)
    {
      size_t idx = (start + step * step) % tableSize;
      probes++;
      if (!occupied[idx])
      {
        occupied[idx] = 1;
        inserted = true;
        break;
      }
    }

    if (!inserted)
      break;

    stats.inserts++;
    stats.totalProbes += probes;
    if (probes > stats.maxProbes)
      stats.maxProbes = probes;
  }

  return stats;
}

static void printStats(const char* label, const ProbeStats& stats)
{
  double avg = stats.inserts ? static_cast<double>(stats.totalProbes) / stats.inserts : 0.0;
  std::printf("  %-10s inserts=%llu avg_probes=%.3f max_probes=%llu\n", label,
              static_cast<unsigned long long>(stats.inserts), avg,
              static_cast<unsigned long long>(stats.maxProbes));
}
} // namespace

int main(int argc, char** argv)
{
  size_t tableSize = 1 << 20; // default: 1,048,576 slots
  if (argc >= 2)
  {
    long parsed = std::strtol(argv[1], nullptr, 10);
    if (parsed > 0)
      tableSize = static_cast<size_t>(parsed);
  }

  const int loadFactorPct[] = {50, 60, 70, 80, 85, 90, 95};

  std::printf("Probing benchmark\n");
  std::printf("table_size=%zu\n", tableSize);
  std::printf("Each row inserts N = floor(table_size * load_factor)\n\n");

  for (int pct : loadFactorPct)
  {
    size_t termsToInsert = (tableSize * static_cast<size_t>(pct)) / 100;
    std::printf("load_factor=%d%% terms=%zu\n", pct, termsToInsert);

    clock_t start = clock();
    ProbeStats linear = benchmarkLinear(tableSize, termsToInsert);
    clock_t mid = clock();
    ProbeStats quadratic = benchmarkQuadratic(tableSize, termsToInsert);
    clock_t end = clock();

    printStats("linear", linear);
    printStats("quadratic", quadratic);

    int linearMs = static_cast<int>((mid - start) * 1000 / CLOCKS_PER_SEC);
    int quadraticMs = static_cast<int>((end - mid) * 1000 / CLOCKS_PER_SEC);
    std::printf("  runtime_ms: linear=%d quadratic=%d\n\n", linearMs, quadraticMs);
  }

  return 0;
}
