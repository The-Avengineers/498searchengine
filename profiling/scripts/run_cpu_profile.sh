#!/usr/bin/env bash
# run_cpu_profile.sh [OUTPUT_DIR [BUILD_DIR]]
#
# Builds avengine + search_benchmark with gprof instrumentation, runs it
# against the search fixture, then produces:
#   gprof_report.txt   — flat profile + call graph (gprof text output)
#   callgraph.svg      — if gprof2dot + dot are available
#   flamegraph.svg     — if perf + FlameGraph are available (Linux only)
#
# Environment variables:
#   BENCHMARK_ITERATIONS  (default 500)
#   BENCHMARK_WARMUP      (default 5)

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUTPUT_DIR="${1:-artifacts/cpu_profile}"
BUILD_DIR="${2:-build-cpu-profile}"

BENCHMARK_ITERATIONS="${BENCHMARK_ITERATIONS:-500}"
BENCHMARK_WARMUP="${BENCHMARK_WARMUP:-5}"

OUTPUT_DIR="$(mkdir -p "$OUTPUT_DIR" && cd "$OUTPUT_DIR" && pwd)"
BUILD_DIR_ABS="$(mkdir -p "$BUILD_DIR" && cd "$BUILD_DIR" && pwd)"

FIXTURE="$REPO_ROOT/profiling/fixtures/search_corpus.tsv"
QUERIES="$REPO_ROOT/profiling/fixtures/benchmark_queries.txt"

echo "==> Configuring with ENABLE_PROFILING=ON"
cmake -S "$REPO_ROOT" -B "$BUILD_DIR_ABS" \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DENABLE_PROFILING=ON \
  -DBUILD_TESTING=OFF \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=OFF \
  > "$OUTPUT_DIR/cmake_configure.log" 2>&1

echo "==> Building index_builder and search_benchmark"
cmake --build "$BUILD_DIR_ABS" \
  --target index_builder search_benchmark \
  --parallel "$(nproc 2>/dev/null || sysctl -n hw.logicalcpu 2>/dev/null || echo 4)" \
  >> "$OUTPUT_DIR/cmake_configure.log" 2>&1

INDEX_BUILDER="$BUILD_DIR_ABS/index_builder"
BENCHMARK="$BUILD_DIR_ABS/search_benchmark"

echo "==> Building index from fixture"
INDEX_DIR="$(mktemp -d)"
trap 'rm -rf "$INDEX_DIR"' EXIT
# index_builder writes .sfi shards to its CWD, so cd there before running.
(cd "$INDEX_DIR" && "$INDEX_BUILDER" "$FIXTURE")

echo "==> Running benchmark (iterations=$BENCHMARK_ITERATIONS warmup=$BENCHMARK_WARMUP)"
# gprof writes gmon.out to CWD — run from OUTPUT_DIR so it lands there.
cd "$OUTPUT_DIR"
"$BENCHMARK" \
  --index-dir "$INDEX_DIR" \
  --queries "$QUERIES" \
  --iterations "$BENCHMARK_ITERATIONS" \
  --warmup "$BENCHMARK_WARMUP" \
  | tee benchmark_results.txt

if [[ ! -f "$OUTPUT_DIR/gmon.out" ]]; then
  echo "WARNING: gmon.out not found; gprof instrumentation may not have worked."
else
  echo "==> Generating gprof report"
  gprof "$BENCHMARK" "$OUTPUT_DIR/gmon.out" > "$OUTPUT_DIR/gprof_report.txt"
  echo "    wrote $OUTPUT_DIR/gprof_report.txt"
  head -60 "$OUTPUT_DIR/gprof_report.txt"

  if command -v gprof2dot > /dev/null 2>&1 && command -v dot > /dev/null 2>&1; then
    echo "==> Generating call-graph SVG"
    gprof2dot "$OUTPUT_DIR/gprof_report.txt" | dot -Tsvg -o "$OUTPUT_DIR/callgraph.svg"
    echo "    wrote $OUTPUT_DIR/callgraph.svg"
  else
    echo "    (skipping callgraph.svg — gprof2dot or dot not found)"
  fi
fi

# perf flamegraph — Linux only, graceful skip
if [[ "$(uname -s)" == "Linux" ]] && command -v perf > /dev/null 2>&1; then
  echo "==> Recording perf profile"
  PERF_DATA="$OUTPUT_DIR/perf.data"
  if perf record -F 997 --call-graph dwarf -o "$PERF_DATA" -- \
       "$BENCHMARK" \
         --index-dir "$INDEX_DIR" \
         --queries "$QUERIES" \
         --iterations "$BENCHMARK_ITERATIONS" \
         --warmup "$BENCHMARK_WARMUP" \
       > /dev/null 2>&1; then
    if command -v stackcollapse-perf.pl > /dev/null 2>&1 && \
       command -v flamegraph.pl > /dev/null 2>&1; then
      echo "==> Generating flamegraph"
      perf script -i "$PERF_DATA" | stackcollapse-perf.pl | flamegraph.pl \
        > "$OUTPUT_DIR/flamegraph.svg"
      echo "    wrote $OUTPUT_DIR/flamegraph.svg"
    else
      echo "    (skipping flamegraph.svg — FlameGraph scripts not in PATH)"
    fi
  else
    echo "    (skipping perf flamegraph — perf record failed, check paranoia setting)"
  fi
fi

echo "==> CPU profile artifacts in $OUTPUT_DIR"
