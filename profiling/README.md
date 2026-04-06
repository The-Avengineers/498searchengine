# Profiling

This directory contains the deterministic profiling fixtures and runners used for CI and local
performance checks.

## What gets measured

- `crawler_index_profile`
  - Offline crawl-side workload using checked-in HTML fixtures
  - Exercises `HtmlParser`, link normalization, `indexDocument()`, and index serialization
- `search_http_profile`
  - Builds a deterministic index from fixture data
  - Launches `search_server`
  - Sends a fixed query set over localhost HTTP and records client/server latency metrics
  - Breaks down per-query timing into `parse_ms`, `solve_ms`, `collect_ms`, and `rank_ms`
- `cpu_profile`
  - Builds `search_benchmark` with gprof instrumentation (`-pg`)
  - Runs queries in a tight loop and generates a gprof flat profile + call graph
  - Optionally generates a flamegraph via `perf` + FlameGraph (Linux only)

The search profile measures the current online search path:
`/api/search` request → `search_server` → shard solve → ISR position collection → ranker.

## Local usage

### Full profiler suite (crawler + search HTTP)

```bash
bash profiling/run_profiles.sh
```

Optional arguments:

```bash
bash profiling/run_profiles.sh <output-dir> <build-dir>
```

Useful environment overrides:

```bash
CRAWLER_REPETITIONS=120 SEARCH_ITERATIONS=20 bash profiling/run_profiles.sh
```

Outputs written to `artifacts/profiling/` by default:

- `crawler_index_profile.json`
- `search_http_profile.json`
- `profile_summary.md`

### Query pipeline HTTP profile (search only)

Run against an already-running server, or let the script start one automatically:

```bash
# Against a running server
bash profiling/scripts/profile_query_pipeline.sh --server-url http://127.0.0.1:8080

# Auto-build + start server from a local index directory
bash profiling/scripts/profile_query_pipeline.sh --index-dir /path/to/shards
```

Options:

| Flag | Default | Description |
|------|---------|-------------|
| `--server-url URL` | — | Use an already-running server |
| `--index-dir DIR` | — | Build + start a server automatically |
| `--build-dir DIR` | `build` | CMake build directory |
| `--iterations N` | `12` | Requests per query |
| `--output FILE` | `artifacts/search_http_profile.json` | Output path |

### CPU profiling (gprof + flamegraph)

Produces function-level call-graph data showing where CPU cycles are spent:

```bash
bash profiling/scripts/run_cpu_profile.sh
```

Optional arguments:

```bash
bash profiling/scripts/run_cpu_profile.sh <output-dir> <build-dir>
```

Environment overrides:

```bash
BENCHMARK_ITERATIONS=1000 BENCHMARK_WARMUP=10 \
  bash profiling/scripts/run_cpu_profile.sh artifacts/cpu_profile build-cpu-profile
```

Outputs written to `artifacts/cpu_profile/` by default:

| File | Description |
|------|-------------|
| `benchmark_results.txt` | Per-query mean/min/max latency table |
| `gprof_report.txt` | gprof flat profile + call graph |
| `callgraph.svg` | Visual call graph (requires `gprof2dot` + `dot`) |
| `flamegraph.svg` | Flamegraph (Linux only; requires `perf` + FlameGraph scripts) |

To install the optional visualization tools on macOS:

```bash
pip install gprof2dot
brew install graphviz
```

#### Running `search_benchmark` directly

```bash
cmake -S . -B build-cpu-profile -DENABLE_PROFILING=ON -DBUILD_TESTING=OFF
cmake --build build-cpu-profile --target search_benchmark -j 4

# gprof writes gmon.out to CWD — run from a writable directory
cd /tmp/my-profile-run
./path/to/build-cpu-profile/search_benchmark \
  --index-dir /path/to/index/shards \
  --queries profiling/fixtures/benchmark_queries.txt \
  --iterations 500 \
  --warmup 5

gprof ./path/to/build-cpu-profile/search_benchmark gmon.out | head -60
```

`ENABLE_PROFILING=OFF` (the default) produces a regular optimized binary; the flag only adds
`-pg -fno-omit-frame-pointer` so the profiling binary is otherwise identical in behavior.

## CI usage

GitHub Actions runs two parallel profiling jobs defined in `.github/workflows/profiling.yml`:

| Job | What it does |
|-----|-------------|
| `profile` | Full crawler + HTTP search profile; uploads JSON + Markdown summary |
| `cpu-profile` | gprof + optional flamegraph; first 60 lines of flat profile go to step summary |

Both jobs upload their artifacts so you can download the full reports from the Actions run page.

## Fixtures

| Path | Used by |
|------|---------|
| `fixtures/search_corpus.tsv` | `search_http_profile`, `cpu_profile` |
| `fixtures/crawl_pages/` | `crawler_index_profile` |
| `fixtures/benchmark_queries.txt` | `search_benchmark` (CPU profiling) |
