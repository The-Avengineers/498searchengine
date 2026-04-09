#!/usr/bin/env bash
set -euo pipefail

DATA_ROOT="${AVENGINE_DATA_ROOT:-/data}"

printf "%-8s %-8s %-10s %-12s %-12s %-12s\n" "worker" "state" "sfi_files" "crawl_rows" "ac_log_mb" "checkpoint"
for worker_dir in "${DATA_ROOT}"/crawlers/*; do
  [[ -d "${worker_dir}" ]] || continue
  worker_id="$(basename "${worker_dir}")"
  pid_file="${DATA_ROOT}/pids/crawler-${worker_id}.pid"
  state="stopped"

  if [[ -f "${pid_file}" ]]; then
    pid="$(cat "${pid_file}")"
    if kill -0 "${pid}" 2>/dev/null; then
      state="running"
    else
      state="stale-pid"
    fi
  fi

  sfi_count="$(find "${worker_dir}" -maxdepth 1 -type f -name '*.sfi' | wc -l | tr -d ' ')"

  crawl_rows=0
  if [[ -f "${worker_dir}/crawl_log.csv" ]]; then
    crawl_rows="$(wc -l < "${worker_dir}/crawl_log.csv" | tr -d ' ')"
  fi

  ac_log_mb="0.0"
  if [[ -f "${worker_dir}/ac_log.tsv" ]]; then
    ac_log_mb="$(du -m "${worker_dir}/ac_log.tsv" | awk '{print $1}')"
  fi

  checkpoint="no"
  if [[ -f "${worker_dir}/frontier_checkpoint.bin" ]]; then
    checkpoint="yes"
  fi

  printf "%-8s %-8s %-10s %-12s %-12s %-12s\n" \
    "${worker_id}" "${state}" "${sfi_count}" "${crawl_rows}" "${ac_log_mb}" "${checkpoint}"
done

echo
current_shards="$(find -L "${DATA_ROOT}/corpus-index/current" -maxdepth 1 -type f -name '*.sfi' 2>/dev/null | wc -l | tr -d ' ')"
worker_shards="$(find "${DATA_ROOT}/crawlers" -type f -name '*.sfi' 2>/dev/null | wc -l | tr -d ' ')"
echo "total worker shards: ${worker_shards}"
echo "current corpus shards: ${current_shards}"
