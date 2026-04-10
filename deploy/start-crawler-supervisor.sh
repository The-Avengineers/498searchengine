#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/data-limit.sh"

DATA_ROOT="${AVENGINE_DATA_ROOT:-/data}"
BIN_ROOT="${AVENGINE_BIN_ROOT:-/avengine/bin}"
PID_FILE="${DATA_ROOT}/pids/crawler-supervisor.pid"
LOG_FILE="${DATA_ROOT}/logs/crawler-supervisor.log"
INTERVAL="${AVENGINE_SUPERVISOR_INTERVAL_SECONDS:-10}"

validate_data_limit_env

mkdir -p "${DATA_ROOT}/pids" "${DATA_ROOT}/logs"

if [[ -f "${PID_FILE}" ]]; then
  existing_pid="$(cat "${PID_FILE}")"
  if kill -0 "${existing_pid}" 2>/dev/null; then
    echo "crawler supervisor already running (pid ${existing_pid})"
    exit 0
  fi
  rm -f "${PID_FILE}"
fi

usage_bytes="$(data_root_usage_bytes)"
if data_limit_reached "${usage_bytes}"; then
  report_data_limit "${usage_bytes}" "crawler supervisor start blocked"
  "${BIN_ROOT}/stop-crawler-swarm.sh" --quiet >> "${LOG_FILE}" 2>&1 || true
  "${BIN_ROOT}/stop-aggregate-watch.sh" >> "${LOG_FILE}" 2>&1 || true
  exit 75
fi

(
  cleanup() {
    rm -f "${PID_FILE}"
  }

  terminate() {
    "${BIN_ROOT}/stop-crawler-swarm.sh" --quiet >> "${LOG_FILE}" 2>&1 || true
    exit 0
  }

  trap cleanup EXIT
  trap terminate TERM INT

  while true; do
    usage_bytes="$(data_root_usage_bytes)"
    if data_limit_reached "${usage_bytes}"; then
      report_data_limit "${usage_bytes}" "crawler supervisor stopped" >> "${LOG_FILE}" 2>&1
      "${BIN_ROOT}/stop-crawler-swarm.sh" --quiet >> "${LOG_FILE}" 2>&1 || true
      "${BIN_ROOT}/stop-aggregate-watch.sh" >> "${LOG_FILE}" 2>&1 || true
      exit 75
    fi

    "${BIN_ROOT}/start-crawler-swarm.sh" >> "${LOG_FILE}" 2>&1 || true
    sleep "${INTERVAL}"
  done
) &

echo $! > "${PID_FILE}"
echo "started crawler supervisor (pid $(cat "${PID_FILE}"))"
