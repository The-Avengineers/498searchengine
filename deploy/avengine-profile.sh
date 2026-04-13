#!/usr/bin/env bash
set -euo pipefail

DATA_ROOT="${AVENGINE_DATA_ROOT:-/data}"
BIN_ROOT="${AVENGINE_BIN_ROOT:-/avengine/bin}"
PROFILE_ROOT="${DATA_ROOT}/profiling"
FIXTURE_QUERIES_FILE="/profiling/fixtures/search_queries.tsv"
LIVE_QUERIES_FILE="/profiling/fixtures/live_search_queries.tsv"
SEARCH_FIXTURE="/profiling/fixtures/search_corpus.tsv"
CRAWL_FIXTURES="/profiling/fixtures/crawl_pages"
LIVE_PROFILE_WAIT_SECONDS="${AVENGINE_LIVE_PROFILE_WAIT_SECONDS:-60}"

OUTPUT_DIR="${1:-${PROFILE_ROOT}}"
BUILD_MODE="${2:-auto}"

mkdir -p "${OUTPUT_DIR}"

echo "Running crawler fixture profile..."
"${BIN_ROOT}/crawler_index_profile" \
  --fixtures "${CRAWL_FIXTURES}" \
  --output "${OUTPUT_DIR}/crawler_index_profile.json" \
  --repetitions "${CRAWLER_REPETITIONS:-80}"

SEARCH_ARGS=()
USING_LIVE_CORPUS=0
QUERIES_FILE="${FIXTURE_QUERIES_FILE}"
if [[ "${BUILD_MODE}" == "fixture" ]]; then
  SEARCH_ARGS+=(--index-builder "${BIN_ROOT}/index_builder" --fixture "${SEARCH_FIXTURE}")
elif [[ "${BUILD_MODE}" == "corpus" ]]; then
  SEARCH_ARGS+=(--index-dir /corpus)
  USING_LIVE_CORPUS=1
  QUERIES_FILE="${LIVE_QUERIES_FILE}"
else
  if find -L /corpus -maxdepth 1 -type f -name '*.sfi' | grep -q .; then
    echo "Using live corpus shards from /corpus for search profile"
    SEARCH_ARGS+=(--index-dir /corpus)
    USING_LIVE_CORPUS=1
    QUERIES_FILE="${LIVE_QUERIES_FILE}"
  else
    echo "No live corpus shards in /corpus; building fixture index for search profile"
    SEARCH_ARGS+=(--index-builder "${BIN_ROOT}/index_builder" --fixture "${SEARCH_FIXTURE}")
  fi
fi

if [[ "${USING_LIVE_CORPUS}" -eq 1 ]]; then
  echo "Using live corpus mode. Waiting ${LIVE_PROFILE_WAIT_SECONDS}s before profiling."
  sleep "${LIVE_PROFILE_WAIT_SECONDS}"
  echo "Refreshing aggregated corpus before profiling..."
  "${BIN_ROOT}/aggregate-corpus-index.sh"
fi

echo "Using query fixture ${QUERIES_FILE}"

# Use the live distributed search server if one is already running, so the
# profile reflects the real serving stack (including any distributed topology).
SEARCH_SERVER_PORT="${AVENGINE_SEARCH_PORT:-8080}"
SEARCH_SERVER_PID_FILE="${DATA_ROOT}/pids/search-server.pid"
LIVE_SERVER_PORT=""
if [[ -f "${SEARCH_SERVER_PID_FILE}" ]]; then
  EXISTING_PID="$(cat "${SEARCH_SERVER_PID_FILE}")"
  if kill -0 "${EXISTING_PID}" 2>/dev/null; then
    LIVE_SERVER_PORT="${SEARCH_SERVER_PORT}"
    echo "Using live search server on port ${LIVE_SERVER_PORT}"
  fi
fi

echo "Running search HTTP profile..."
if [[ -n "${LIVE_SERVER_PORT}" ]]; then
  python3 /profiling/scripts/run_search_http_profile.py \
    --live-server-port "${LIVE_SERVER_PORT}" \
    --queries-file "${QUERIES_FILE}" \
    --output "${OUTPUT_DIR}/search_http_profile.json" \
    --iterations "${SEARCH_ITERATIONS:-12}"
else
  python3 /profiling/scripts/run_search_http_profile.py \
    --search-server "${BIN_ROOT}/search_server" \
    --queries-file "${QUERIES_FILE}" \
    --output "${OUTPUT_DIR}/search_http_profile.json" \
    --iterations "${SEARCH_ITERATIONS:-12}" \
    "${SEARCH_ARGS[@]}"
fi

python3 /profiling/scripts/render_ci_profile_summary.py \
  --crawler-json "${OUTPUT_DIR}/crawler_index_profile.json" \
  --search-json "${OUTPUT_DIR}/search_http_profile.json" \
  --output "${OUTPUT_DIR}/profile_summary.md"

cat "${OUTPUT_DIR}/profile_summary.md"
echo
echo "Profiling artifacts written to ${OUTPUT_DIR}"
