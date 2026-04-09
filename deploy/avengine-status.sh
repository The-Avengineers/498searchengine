#!/usr/bin/env bash
set -euo pipefail

DATA_ROOT="${AVENGINE_DATA_ROOT:-/data}"

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

worker_count=0
running_workers=0
shopt -s nullglob
for pid_file in "${DATA_ROOT}"/pids/crawler-*.pid; do
  worker_count=$((worker_count + 1))
  pid="$(cat "${pid_file}")"
  if kill -0 "${pid}" 2>/dev/null; then
    running_workers=$((running_workers + 1))
  fi
done

echo "crawler swarm: ${running_workers}/${worker_count} worker pid file(s) active"
report_pid "aggregate watcher" "${DATA_ROOT}/pids/aggregate.pid"
report_pid "search server" "${DATA_ROOT}/pids/search-server.pid"

if [[ -d "${DATA_ROOT}/corpus-index/current" ]]; then
  shard_count="$(find "${DATA_ROOT}/corpus-index/current" -maxdepth 1 -type f -name '*.sfi' | wc -l)"
  echo "current corpus shards: ${shard_count}"
else
  echo "current corpus shards: 0"
fi

echo
echo "logs:"
echo "  crawler logs: /logs"
echo "  aggregate log: ${DATA_ROOT}/aggregate.log"
echo "  search log: ${DATA_ROOT}/logs/search-server.log"
