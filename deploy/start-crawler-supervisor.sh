#!/usr/bin/env bash
set -euo pipefail

DATA_ROOT="${AVENGINE_DATA_ROOT:-/data}"
BIN_ROOT="${AVENGINE_BIN_ROOT:-/avengine/bin}"
PID_FILE="${DATA_ROOT}/pids/crawler-supervisor.pid"
LOG_FILE="${DATA_ROOT}/logs/crawler-supervisor.log"
INTERVAL="${AVENGINE_SUPERVISOR_INTERVAL_SECONDS:-10}"

mkdir -p "${DATA_ROOT}/pids" "${DATA_ROOT}/logs"

if [[ -f "${PID_FILE}" ]]; then
  existing_pid="$(cat "${PID_FILE}")"
  if kill -0 "${existing_pid}" 2>/dev/null; then
    echo "crawler supervisor already running (pid ${existing_pid})"
    exit 0
  fi
  rm -f "${PID_FILE}"
fi

(
  while true; do
    "${BIN_ROOT}/start-crawler-swarm.sh" >> "${LOG_FILE}" 2>&1 || true
    sleep "${INTERVAL}"
  done
) &

echo $! > "${PID_FILE}"
echo "started crawler supervisor (pid $(cat "${PID_FILE}"))"
