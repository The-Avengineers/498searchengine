#!/usr/bin/env bash
# profile_query_pipeline.sh — locally profile the search query pipeline
#
# Usage:
#   ./profile_query_pipeline.sh [options]
#
# Options:
#   --server-url URL     Base URL of a running search_server (skip auto-start)
#   --index-dir DIR      Use pre-built .sfi shards from DIR instead of building a fresh index
#   --build-dir DIR      Directory containing built binaries (default: <repo>/build-profiling)
#   --queries-file FILE  TSV query fixture (default: profiling/fixtures/search_queries.tsv)
#   --iterations N       Queries per term (default: 20)
#   --output FILE        Write JSON summary to FILE (stdout only by default)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
FIXTURE="${REPO_ROOT}/profiling/fixtures/search_corpus.tsv"
QUERIES_FILE="${REPO_ROOT}/profiling/fixtures/search_queries.tsv"

# ── Defaults ──────────────────────────────────────────────────────────────────
SERVER_URL=""
INDEX_DIR=""          # empty = build a fresh index from fixture
BUILD_DIR="${REPO_ROOT}/build-profiling"
ITERATIONS=20
OUTPUT_FILE=""
MANAGED_SERVER_PID=""
MANAGED_INDEX_DIR=""  # set when we create a tmpdir for the fresh index

# ── Argument parsing ──────────────────────────────────────────────────────────
while [[ $# -gt 0 ]]; do
  case "$1" in
    --server-url) SERVER_URL="$2"; shift 2 ;;
    --index-dir)  INDEX_DIR="$2";  shift 2 ;;
    --build-dir)  BUILD_DIR="$2";  shift 2 ;;
    --queries-file) QUERIES_FILE="$2"; shift 2 ;;
    --iterations) ITERATIONS="$2"; shift 2 ;;
    --output)     OUTPUT_FILE="$2"; shift 2 ;;
    *) echo "Unknown option: $1" >&2; exit 1 ;;
  esac
done

if [[ "${BUILD_DIR}" != /* ]]; then
  BUILD_DIR="${PWD}/${BUILD_DIR}"
fi

if ! command -v python3 &>/dev/null; then
  echo "python3 is required" >&2
  exit 1
fi

# ── Server lifecycle ──────────────────────────────────────────────────────────
cleanup() {
  if [[ -n "${MANAGED_SERVER_PID}" ]]; then
    kill "${MANAGED_SERVER_PID}" 2>/dev/null || true
    wait "${MANAGED_SERVER_PID}" 2>/dev/null || true
  fi
  if [[ -n "${MANAGED_INDEX_DIR}" ]]; then
    rm -rf "${MANAGED_INDEX_DIR}"
  fi
}
trap cleanup EXIT

if [[ -z "${SERVER_URL}" ]]; then
  SEARCH_SERVER="${BUILD_DIR}/search_server"
  INDEX_BUILDER="${BUILD_DIR}/index_builder"

  echo "Building binaries..." >&2
  cmake -S "${REPO_ROOT}" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release \
    --log-level=WARNING >/dev/null
  cmake --build "${BUILD_DIR}" --config Release \
    --target search_server index_builder -j >/dev/null

  # Build a fresh index from the fixture if no --index-dir was given.
  if [[ -z "${INDEX_DIR}" ]]; then
    MANAGED_INDEX_DIR="$(mktemp -d)"
    echo "Building fresh index from fixture into ${MANAGED_INDEX_DIR}..." >&2
    (cd "${MANAGED_INDEX_DIR}" && "${INDEX_BUILDER}" "${FIXTURE}") >/dev/null
    INDEX_DIR="${MANAGED_INDEX_DIR}"
  fi

  SHARD_COUNT="$(ls "${INDEX_DIR}"/*.sfi 2>/dev/null | wc -l | tr -d ' ')"
  PORT=$(( 20000 + ($$ % 20000) ))
  echo "Starting search_server on port ${PORT} with ${SHARD_COUNT} shard(s) from ${INDEX_DIR}..." >&2
  "${SEARCH_SERVER}" "${PORT}" "${INDEX_DIR}" >/dev/null 2>&1 &
  MANAGED_SERVER_PID=$!

  # Wait for the server to accept connections.
  if ! python3 - "${PORT}" <<'PYEOF'
import socket, sys, time
port = int(sys.argv[1])
deadline = time.time() + 15
while time.time() < deadline:
    with socket.socket() as s:
        s.settimeout(0.25)
        if s.connect_ex(("127.0.0.1", port)) == 0:
            sys.exit(0)
    time.sleep(0.1)
sys.exit(1)
PYEOF
  then
    echo "search_server did not start in time" >&2
    exit 1
  fi

  SERVER_URL="http://127.0.0.1:${PORT}"
  echo "Server ready at ${SERVER_URL}" >&2
fi

# ── All measurement and reporting delegated to Python ────────────────────────
python3 - "${SERVER_URL}" "${ITERATIONS}" "${OUTPUT_FILE}" "${QUERIES_FILE}" "${SCRIPT_DIR}" <<'PYEOF'
from __future__ import annotations
import json, pathlib, statistics, sys, time, urllib.request, urllib.parse

server_url   = sys.argv[1]
iterations   = int(sys.argv[2])
output_file  = sys.argv[3]  # empty string means no file output
queries_file = pathlib.Path(sys.argv[4]).resolve()
script_dir   = pathlib.Path(sys.argv[5]).resolve()

sys.path.insert(0, str(script_dir))
from query_fixture import load_search_queries  # noqa: E402

QUERIES = load_search_queries(queries_file)

def fetch(query: str, limit: int) -> dict:
    url = server_url + "/api/search?" + urllib.parse.urlencode({"q": query, "limit": limit})
    start = time.perf_counter()
    try:
        with urllib.request.urlopen(url, timeout=5) as resp:
            data = resp.read()
    except Exception as exc:
        return {"error": str(exc), "client_ms": 0.0, "total_hits": 0,
                "took_ms": 0, "parse_ms": 0, "solve_ms": 0,
                "collect_ms": 0, "rank_ms": 0}
    elapsed_ms = (time.perf_counter() - start) * 1000.0
    d = json.loads(data)
    d["client_ms"] = elapsed_ms
    return d

def pct(vals: list[float], p: float) -> float:
    if not vals:
        return 0.0
    ordered = sorted(vals)
    if len(ordered) == 1:
        return ordered[0]
    idx = p * (len(ordered) - 1)
    lo, hi = int(idx), min(int(idx) + 1, len(ordered) - 1)
    return ordered[lo] + (ordered[hi] - ordered[lo]) * (idx - lo)

def mean(vals: list[float]) -> float:
    return statistics.fmean(vals) if vals else 0.0

COL_W = 44
print()
print(f"{'Query':<{COL_W}}  {'Hits':>4}  {'Mean':>7}  {'P50':>7}  {'P95':>7}"
      f"  {'Parse':>7}  {'Solve':>7}  {'Collect':>7}  {'Rank':>7}")
print("─" * 102)

samples = []
for spec in QUERIES:
    fetch(spec.query, spec.limit)  # warmup (discarded)

    client_ms_list: list[float] = []
    took_list:      list[float] = []
    parse_list:     list[float] = []
    solve_list:     list[float] = []
    collect_list:   list[float] = []
    rank_list:      list[float] = []
    error_count_list: list[int] = []
    actual_hits = 0

    for _ in range(iterations):
        r = fetch(spec.query, spec.limit)
        client_ms_list.append(float(r.get("client_ms", 0)))
        took_list.append(    float(r.get("took_ms",    0)))
        parse_list.append(   float(r.get("parse_ms",   0)))
        solve_list.append(   float(r.get("solve_ms",   0)))
        collect_list.append( float(r.get("collect_ms", 0)))
        rank_list.append(    float(r.get("rank_ms",    0)))
        error_count_list.append(len(r.get("errors", [])))
        actual_hits = int(r.get("total_hits", 0))

    m_client  = mean(client_ms_list)
    p50       = pct(client_ms_list, 0.50)
    p95       = pct(client_ms_list, 0.95)
    m_parse   = mean(parse_list)
    m_solve   = mean(solve_list)
    m_collect = mean(collect_list)
    m_rank    = mean(rank_list)

    print(f"{spec.label:<{COL_W}}  {actual_hits:>4}  {m_client:>7.1f}  {p50:>7.1f}  {p95:>7.1f}"
          f"  {m_parse:>7.1f}  {m_solve:>7.1f}  {m_collect:>7.1f}  {m_rank:>7.1f}", flush=True)

    samples.append({
        "query":            spec.query,
        "label":            spec.label,
        "limit":            spec.limit,
        "iterations":       iterations,
        "total_hits":       actual_hits,
        "mean_client_ms":   round(m_client,        3),
        "p50_client_ms":    round(p50,             3),
        "p95_client_ms":    round(p95,             3),
        "mean_took_ms":     round(mean(took_list),  3),
        "mean_parse_ms":    round(m_parse,          3),
        "mean_solve_ms":    round(m_solve,          3),
        "mean_collect_ms":  round(m_collect,        3),
        "mean_rank_ms":     round(m_rank,           3),
        "mean_error_count": round(mean(error_count_list), 3),
    })

print("─" * 102)
print("Columns (ms): client round-trip (mean/P50/P95) | server-side phase breakdowns\n")

if output_file:
    payload = {
        "profile":              "query_pipeline",
        "server_url":           server_url,
        "queries_file":         str(queries_file),
        "iterations_per_query": iterations,
        "samples":              samples,
    }
    pathlib.Path(output_file).write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    print(f"JSON written to {output_file}", file=sys.stderr)
PYEOF
