#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/data-limit.sh"

DATA_ROOT="${AVENGINE_DATA_ROOT:-/data}"
BIN_ROOT="${AVENGINE_BIN_ROOT:-/avengine/bin}"
PID_FILE="${DATA_ROOT}/pids/aggregate.pid"
LOG_FILE="${DATA_ROOT}/aggregate.log"
INTERVAL="${AVENGINE_AGGREGATE_INTERVAL_SECONDS:-300}"

validate_data_limit_env

mkdir -p "${DATA_ROOT}/pids"

if [[ -f "${PID_FILE}" ]]; then
  existing_pid="$(cat "${PID_FILE}")"
  if kill -0 "${existing_pid}" 2>/dev/null; then
    echo "aggregate watcher already running (pid ${existing_pid})"
    exit 0
  fi
  rm -f "${PID_FILE}"
fi

usage_bytes="$(data_root_usage_bytes)"
if data_limit_reached "${usage_bytes}"; then
  report_data_limit "${usage_bytes}" "aggregate watcher start blocked"
  "${BIN_ROOT}/stop-crawler-swarm.sh" --quiet >> "${LOG_FILE}" 2>&1 || true
  "${BIN_ROOT}/stop-crawler-supervisor.sh" >> "${LOG_FILE}" 2>&1 || true
  exit 75
fi

(
  child_pid=""

  cleanup() {
    rm -f "${PID_FILE}"
  }

  terminate() {
    if [[ -n "${child_pid}" ]] && kill -0 "${child_pid}" 2>/dev/null; then
      kill "${child_pid}" 2>/dev/null || true
      wait "${child_pid}" 2>/dev/null || true
    fi
    exit 0
  }

  trap cleanup EXIT
  trap terminate TERM INT

  "${BIN_ROOT}/aggregate-corpus-index.sh" --watch --interval "${INTERVAL}" >> "${LOG_FILE}" 2>&1 &
  child_pid=$!
  wait "${child_pid}"
) &
echo $! > "${PID_FILE}"
echo "started aggregate watcher (pid $(cat "${PID_FILE}"))"
