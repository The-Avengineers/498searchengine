#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

OUTPUT_DIR="${1:-${REPO_ROOT}/artifacts/profiling}"
BUILD_DIR="${2:-${REPO_ROOT}/build-profiling}"
CRAWLER_REPETITIONS="${CRAWLER_REPETITIONS:-80}"
SEARCH_ITERATIONS="${SEARCH_ITERATIONS:-12}"

mkdir -p "${OUTPUT_DIR}"

cmake -S "${REPO_ROOT}" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release
cmake --build "${BUILD_DIR}" --config Release --target \
  crawler_index_profile \
  index_builder \
  search_server -j

"${BUILD_DIR}/crawler_index_profile" \
  --fixtures "${REPO_ROOT}/profiling/fixtures/crawl_pages" \
  --output "${OUTPUT_DIR}/crawler_index_profile.json" \
  --repetitions "${CRAWLER_REPETITIONS}"

python3 "${REPO_ROOT}/profiling/scripts/run_search_http_profile.py" \
  --index-builder "${BUILD_DIR}/index_builder" \
  --search-server "${BUILD_DIR}/search_server" \
  --fixture "${REPO_ROOT}/profiling/fixtures/search_corpus.tsv" \
  --output "${OUTPUT_DIR}/search_http_profile.json" \
  --iterations "${SEARCH_ITERATIONS}"

python3 "${REPO_ROOT}/profiling/scripts/render_ci_profile_summary.py" \
  --crawler-json "${OUTPUT_DIR}/crawler_index_profile.json" \
  --search-json "${OUTPUT_DIR}/search_http_profile.json" \
  --output "${OUTPUT_DIR}/profile_summary.md"

printf 'Profiling artifacts written to %s\n' "${OUTPUT_DIR}"
