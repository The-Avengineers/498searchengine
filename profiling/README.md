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

The search profile measures the current online search path that exists in this repo today:
`/api/search` request sent to `search_server` and results returned. It does not try to simulate a
browser-rendering flow because the frontend is still static HTML.

## Local usage

Run the full profiler suite:

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

Outputs:

- `crawler_index_profile.json`
- `search_http_profile.json`
- `profile_summary.md`

Default output location:

```text
artifacts/profiling/
```

## CI usage

GitHub Actions calls the same bash runner from `.github/workflows/profiling.yml` and uploads the
generated JSON plus the Markdown summary as workflow artifacts.
