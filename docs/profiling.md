# Profiling

The profiling setup measures two independent workloads: the offline crawl-and-index pipeline, and the online search query pipeline. Both have small deterministic fixtures for CI and support pointing at a larger real-world corpus locally or in CI via a GitHub Release.

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

The HTTP query profile reads its query set from `profiling/fixtures/search_queries.tsv`. Each row
defines a label, query text, and limit; profiles report the observed hit counts from the corpus
being tested.

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
| `profile_summary.md` | Markdown table rendered to CI step summary |

---

## Corpus index for realistic benchmarking

The fixture corpus (`profiling/fixtures/search_corpus.tsv`) is tiny — good for correctness and fast CI, but not representative of real query latency. For realistic numbers use the full crawl index.

### Uploading the corpus index to GitHub Releases

The `.sfi` shard files (100–200 MB each) are gitignored. Upload them once to a GitHub Release so CI can download them:

```bash
# From the repo root (where .sfi files live after a crawl):
bash profiling/scripts/upload_corpus_index.sh

# Or specify a different directory:
bash profiling/scripts/upload_corpus_index.sh /path/to/shards
```

This creates (or replaces) a release tagged `corpus-index-latest` with all `.sfi` files as assets. Requires the `gh` CLI authenticated with write access to the repo.

### Updating the corpus

Re-run the upload script after a new crawl. If you want to guarantee CI doesn't use a stale cached copy, bump the tag:

1. Edit the `TAG` variable in `upload_corpus_index.sh` (e.g., `corpus-index-v2`)
2. Update `CORPUS_INDEX_TAG` in `.github/workflows/profiling.yml` to match
3. Push — CI will download the new release on the next run

---

## CI

Two jobs are defined in `.github/workflows/profiling.yml`:

| Job | Trigger | What it does |
|-----|---------|-------------|
| `profile` | PRs + pushes to main/master | Full crawler + HTTP search profile using fixture corpus; uploads JSON + Markdown summary |
| `corpus-profile` | Pushes to main/master only | Downloads the large corpus index from GitHub Releases (cached), runs 50 iterations per query, uploads `corpus_profile.json` |

The `corpus-profile` job is skipped on PRs to avoid downloading ~1 GB of index data on every review cycle. Both jobs upload their artifacts so you can download the full reports from the Actions run page.

A separate scheduled/manual workflow lives at `.github/workflows/live-internet-crawl.yml`. It runs
the crawler box against the public internet for a bounded interval, aggregates the resulting shards,
profiles `search_server` against that live corpus, and refreshes the `corpus-index-latest` release
used by `corpus-profile`.

---

## Fixtures

| Path | Used by |
|------|---------|
| `profiling/fixtures/search_corpus.tsv` | `search_http_profile`, `profile_query_pipeline` (default) |
| `profiling/fixtures/search_queries.tsv` | `search_http_profile`, `profile_query_pipeline` |
| `profiling/fixtures/crawl_pages/` | `crawler_index_profile` |
