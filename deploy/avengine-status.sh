#!/usr/bin/env bash
set -euo pipefail

DATA_ROOT="${AVENGINE_DATA_ROOT:-/data}"
MAX_DATA_BYTES="${AVENGINE_MAX_DATA_BYTES:-51539607552}"

format_bytes() {
  if command -v numfmt >/dev/null 2>&1; then
    numfmt --to=iec-i --suffix=B "$1"
  else
    printf '%s B\n' "$1"
  fi
}

report_pid() {
  local label="$1"
  local pid_file="$2"

  if [[ -f "${pid_file}" ]]; then
    local pid
    pid="$(cat "${pid_file}")"
    if kill -0 "${pid}" 2>/dev/null; then
      echo "${label}: running (pid ${pid})"
    else
      echo "${label}: stale pid file (${pid})"
    fi
  else
    echo "${label}: stopped"
  fi
}

echo "layout:"
echo "  /avengine  /config  /data  /logs  /crawlers  /corpus  /profiling"
echo

data_usage_bytes="$(du -sb "${DATA_ROOT}" 2>/dev/null | cut -f1 || echo 0)"
if [[ "${MAX_DATA_BYTES}" =~ ^[0-9]+$ ]] && (( MAX_DATA_BYTES > 0 )); then
  echo "data usage: $(format_bytes "${data_usage_bytes}") / $(format_bytes "${MAX_DATA_BYTES}")"
else
  echo "data usage: $(format_bytes "${data_usage_bytes}")"
fi
if [[ "${MAX_DATA_BYTES}" =~ ^[0-9]+$ ]] && (( MAX_DATA_BYTES > 0 && data_usage_bytes >= MAX_DATA_BYTES )); then
  echo "aggregate limit: reached"
fi
echo

worker_count=0
running_workers=0
stale_workers=0
shopt -s nullglob
for pid_file in "${DATA_ROOT}"/pids/crawler-*.pid; do
  worker_count=$((worker_count + 1))
  pid="$(cat "${pid_file}")"
  if kill -0 "${pid}" 2>/dev/null; then
    running_workers=$((running_workers + 1))
  else
    stale_workers=$((stale_workers + 1))
  fi
done

echo "crawler swarm: ${running_workers}/${worker_count} worker pid file(s) active"
if (( stale_workers > 0 )); then
  echo "crawler swarm: ${stale_workers} stale worker pid file(s)"
fi
report_pid "crawler supervisor" "${DATA_ROOT}/pids/crawler-supervisor.pid"
report_pid "aggregate watcher" "${DATA_ROOT}/pids/aggregate.pid"
report_pid "search server" "${DATA_ROOT}/pids/search-server.pid"

if [[ -d "${DATA_ROOT}/corpus-index/current" ]]; then
  shard_count="$(find -L "${DATA_ROOT}/corpus-index/current" -maxdepth 1 -type f -name '*.sfi' | wc -l | tr -d ' ')"
  echo "current corpus shards: ${shard_count}"
else
  echo "current corpus shards: 0"
fi

if [[ -d "${DATA_ROOT}/corpus-profile/current" ]]; then
  profile_shard_count="$(find -L "${DATA_ROOT}/corpus-profile/current" -maxdepth 1 -type f -name '*.sfi' | wc -l | tr -d ' ')"
  echo "promoted profile shards: ${profile_shard_count}"
else
  echo "promoted profile shards: 0"
fi

echo
echo "logs:"
echo "  crawler logs: /logs"
echo "  supervisor log: ${DATA_ROOT}/logs/crawler-supervisor.log"
echo "  aggregate log: ${DATA_ROOT}/aggregate.log"
echo "  search log: ${DATA_ROOT}/logs/search-server.log"
