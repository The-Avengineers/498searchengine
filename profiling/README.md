# Profiling

This directory contains the deterministic profiling fixtures and runners used for CI and local performance checks.

See **[docs/profiling.md](../docs/profiling.md)** for full documentation including local usage, CI setup, and the large corpus index workflow.

## Quick start

```bash
# Ad-hoc query pipeline profile (builds a fresh index automatically):
bash profiling/scripts/profile_query_pipeline.sh

# Full crawler + HTTP search suite (used by CI):
bash profiling/run_profiles.sh

# Profile against the large real-world corpus:
bash profiling/scripts/profile_query_pipeline.sh --index-dir /path/to/shards
```

The search query set is defined in `fixtures/search_queries.tsv`. It is shared by local and CI
profiling, and the reported hit counts come from the corpus actually being profiled.

## Directory layout

| Path | Description |
|------|-------------|
| `run_profiles.sh` | CI runner — crawler profile + HTTP search profile + Markdown summary |
| `scripts/profile_query_pipeline.sh` | Ad-hoc query latency profiler with per-phase breakdown |
| `scripts/upload_corpus_index.sh` | Optional backup/distribution helper for publishing `.sfi` shards to GitHub Releases |
| `scripts/run_search_http_profile.py` | HTTP search profiler used by `run_profiles.sh` |
| `scripts/query_fixture.py` | Shared TSV loader for search query fixtures |
| `scripts/render_ci_profile_summary.py` | Renders JSON results as a Markdown table |
| `fixtures/search_corpus.tsv` | Small deterministic search corpus for fixture runs |
| `fixtures/search_queries.tsv` | Shared search query fixture used by local/CI profiling |
| `fixtures/crawl_pages/` | HTML fixtures for the crawler + indexer profile |
