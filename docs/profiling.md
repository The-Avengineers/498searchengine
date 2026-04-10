# Profiling

The profiling setup measures two independent workloads: the offline crawl-and-index pipeline, and the online search query pipeline. Both have small deterministic fixtures for CI and support pointing at a larger real-world corpus locally or in CI.

## What gets measured

### Crawler + indexer (`crawler_index_profile`)

Exercises the offline path end-to-end using checked-in HTML fixtures:

- `HtmlParser` — tag stripping, text extraction
- Link normalization
- `indexDocument()` — tokenization, posting list construction
- Index serialization — shard write + atomic rename

Reported metrics: documents/sec, MB/sec, parse+index ms, serialize ms.

### Search HTTP profile (`search_http_profile` / `profile_query_pipeline`)

Builds a deterministic index, starts `search_server`, and drives it over localhost HTTP. Measures the full online path:

```
GET /api/search → search_server → shard solve → ISR position collect → ranker
```

Per-query timing fields returned by the server and recorded in the profile:

| Field | What it covers |
|-------|---------------|
| `parse_ms` | Query lexing and AST construction |
| `solve_ms` | Constraint solver — ISR traversal across all shards |
| `collect_ms` | ISR position collection for ranking signals |
| `rank_ms` | Scoring and top-K selection |

Reported statistics per query: mean client round-trip, P50, P95, plus the server-side phase breakdown above.

For the live corpus profile, `profiling/fixtures/live_search_queries.tsv` may also include a
`group` column. Rows tagged `coverage` are rendered in a dedicated "Query Shape Coverage" section
so CI always shows phrase / AND / OR / grouped-query probes even when they are not among the
highest-hit queries.

### Search CPU callgraph (`run_search_cpu_profile.py`)

Builds the instrumented `search_benchmark` binary with `-DENABLE_PROFILING=ON`, runs the same
search code path in-process against a shard corpus, and emits a `gprof` callgraph:

```
search_benchmark → SearchService → solver / collect / ranker
```

This profile is intended to show where CPU time is actually going. It excludes HTTP/network
overhead and reports function-level hotspots instead of latency percentiles.

The HTTP query profile reads its query set from `profiling/fixtures/search_queries.tsv`. Each row
defines a label, query text, and limit; profiles report the observed hit counts from the corpus
being tested.

For live-corpus profiling, use `profiling/fixtures/live_search_queries.tsv` instead of the fixture
query set. The live query fixture is intended for real crawl shards and should not be expected to
produce deterministic hit counts across runs.

---

## Local usage

### Quick ad-hoc profile (fixture corpus, auto-managed server)

Builds a fresh index from the fixture and runs the query pipeline profile:

```bash
bash profiling/scripts/profile_query_pipeline.sh
```

Sample output:

```
Query                                         Hits     Mean      P50      P95    Parse    Solve  Collect     Rank
──────────────────────────────────────────────────────────────────────────────────────────────────────
apple (common)                                   4      0.2      0.2      0.3      0.0      0.0      0.0      0.0
search ranking (multi-term)                      2      0.2      0.2      0.3      0.0      0.0      0.0      0.0
...
```

Options:

| Flag | Default | Description |
|------|---------|-------------|
| `--server-url URL` | — | Connect to a running server instead of starting one |
| `--index-dir DIR` | — | Use pre-built `.sfi` shards from DIR instead of building from fixture |
| `--build-dir DIR` | `build-profiling` | CMake build directory |
| `--queries-file FILE` | `profiling/fixtures/search_queries.tsv` | TSV query fixture with labels, queries, and limits |
| `--iterations N` | `20` | Query iterations per term (first is warmup, discarded) |
| `--output FILE` | — | Write JSON summary to FILE |

### Profile against the large real-world corpus

If you have the crawl index shards locally (`.sfi` files in the repo root after a crawl run):

```bash
bash profiling/scripts/profile_query_pipeline.sh --index-dir .
```

Or point at any directory containing `.sfi` files:

```bash
bash profiling/scripts/profile_query_pipeline.sh --index-dir /path/to/shards --iterations 50
```

### Run the CPU callgraph locally

```bash
cmake -S . -B build-gprof -DCMAKE_BUILD_TYPE=Release -DENABLE_PROFILING=ON
cmake --build build-gprof --target search_benchmark -j

python3 profiling/scripts/run_search_cpu_profile.py \
  --search-benchmark build-gprof/search_benchmark \
  --index-dir /path/to/shards \
  --queries-file profiling/fixtures/live_search_queries.tsv \
  --iterations 100 \
  --warmup 5 \
  --output-json artifacts/corpus_cpu_profile.json \
  --output-report artifacts/corpus_cpu_profile_gprof.txt \
  --output-benchmark artifacts/corpus_cpu_profile_benchmark.txt \
  --output-query-list artifacts/corpus_cpu_profile_queries.txt
```

### Full CI suite (crawler + HTTP search)

Runs both the crawler profile and the HTTP search profile and renders a Markdown summary:

```bash
bash profiling/run_profiles.sh
# outputs go to artifacts/profiling/ by default
```

Optional arguments:

```bash
bash profiling/run_profiles.sh <output-dir> <build-dir>
CRAWLER_REPETITIONS=120 SEARCH_ITERATIONS=30 bash profiling/run_profiles.sh
```

Outputs:

| File | Description |
|------|-------------|
| `crawler_index_profile.json` | Crawler + indexer metrics |
| `search_http_profile.json` | Per-query latency + phase breakdown |
| `corpus_cpu_profile.json` | Parsed CPU hotspot summary |
| `corpus_cpu_profile_gprof.txt` | Raw `gprof` callgraph |
| `profile_summary.md` | Markdown table rendered to CI step summary |

---

## Corpus index for realistic benchmarking

The fixture corpus (`profiling/fixtures/search_corpus.tsv`) is tiny — good for correctness and fast CI, but not representative of real query latency. For realistic numbers use the full crawl index.

### Persistent profiling corpus on the runtime container

The self-hosted runner now treats the runtime container volume as the source of truth for CI query profiling:

- `/data/corpus-index/current` is the latest aggregated live crawl output
- `/data/corpus-profile/current` is the promoted profiling snapshot used by the `corpus-profile` workflow

Inside the container, promote the latest aggregated crawl into the persistent profiling location with:

```bash
av-promote-corpus-profile
```

That command snapshots the current aggregated shard set into `/data/corpus-profile/current` using an atomic generation switch, so pull requests and manual profiling runs reuse a stable shard set until the next promotion.

For clarity: in-container `av-profile ... corpus` still reads `/corpus` (the live aggregated corpus), while GitHub Actions `corpus-profile` reads the promoted snapshot from `/data/corpus-profile/current`.

### Optional GitHub Release backup

If you still want a release-backed archive for backup or distribution, the upload helper remains available:

```bash
bash profiling/scripts/upload_corpus_index.sh /path/to/shards
```

---

## CI

The profiling workflow now runs a single `corpus-profile` job in `.github/workflows/profiling.yml`.
It runs on the repo's dedicated `avengine-runtime` self-hosted runner for all pushes, pull requests, and manual dispatches.

`corpus-profile`:

- copies the promoted `.sfi` shard set from `avengine-runtime:/var/lib/avengine/corpus-profile/current`
- profiles `search_server` against those shards using `profiling/fixtures/live_search_queries.tsv`
- pins `SEARCH_SHARD_WORKERS=8` during the profile run so shard-parallel latency comparisons remain stable
- builds `search_benchmark` with `-DENABLE_PROFILING=ON` and profiles it with `gprof` against those same shards
- uploads `artifacts/corpus_profile.json` plus raw CPU profiler artifacts
- writes both the latency summary and the CPU callgraph summary to the GitHub Actions step summary
- pins `SEARCH_SHARD_WORKERS=8` during the HTTP profile run so shard-parallel latency comparisons remain stable
- fails fast if the runtime container is down or the promoted profiling corpus has not been built yet
- fails if shards load successfully but every live query returns zero hits
- fails if the CPU profiler run completes but produces no parsed hotspot functions

A separate push-to-`master` / manual workflow lives at `.github/workflows/live-internet-crawl.yml`.
It talks to the already-running crawler service on that same dedicated runtime runner, refreshes the
aggregated corpus, promotes it into `/data/corpus-profile/current`, and profiles `search_server`
against that promoted corpus. Pull requests reuse the latest promoted snapshot instead of rebuilding it.

The long-lived runtime service itself is deployed by `.github/workflows/deploy-avengine.yml` on push to `master` or by manual dispatch.

---

## Fixtures

| Path | Used by |
|------|---------|
| `profiling/fixtures/search_corpus.tsv` | `search_http_profile`, `profile_query_pipeline` (default) |
| `profiling/fixtures/search_queries.tsv` | `search_http_profile`, `profile_query_pipeline` |
| `profiling/fixtures/live_search_queries.tsv` | live corpus profiling in CI and `av-profile ... corpus` |
| `profiling/fixtures/crawl_pages/` | `crawler_index_profile` |
