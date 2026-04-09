#!/usr/bin/env bash
set -euo pipefail

DATA_ROOT="${AVENGINE_DATA_ROOT:-/data}"
BIN_ROOT="${AVENGINE_BIN_ROOT:-/avengine/bin}"
PID_FILE="${DATA_ROOT}/pids/aggregate.pid"
LOG_FILE="${DATA_ROOT}/aggregate.log"
INTERVAL="${AVENGINE_AGGREGATE_INTERVAL_SECONDS:-300}"

mkdir -p "${DATA_ROOT}/pids"

if [[ -f "${PID_FILE}" ]]; then
  existing_pid="$(cat "${PID_FILE}")"
  if kill -0 "${existing_pid}" 2>/dev/null; then
    echo "aggregate watcher already running (pid ${existing_pid})"
    exit 0
  fi
  rm -f "${PID_FILE}"
fi

"${BIN_ROOT}/aggregate-corpus-index.sh" --watch --interval "${INTERVAL}" >> "${LOG_FILE}" 2>&1 &
echo $! > "${PID_FILE}"
echo "started aggregate watcher (pid $(cat "${PID_FILE}"))"
