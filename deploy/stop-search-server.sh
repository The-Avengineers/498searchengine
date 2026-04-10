#!/usr/bin/env bash
set -euo pipefail

DATA_ROOT="${AVENGINE_DATA_ROOT:-/data}"
PID_FILE="${DATA_ROOT}/pids/search-server.pid"

if [[ ! -f "${PID_FILE}" ]]; then
  echo "search server is not running"
  exit 0
fi

pid="$(cat "${PID_FILE}")"
if kill -0 "${pid}" 2>/dev/null; then
  kill "${pid}" 2>/dev/null || true
  for _ in $(seq 1 10); do
    if ! kill -0 "${pid}" 2>/dev/null; then
      break
    fi
    sleep 1
  done
  if kill -0 "${pid}" 2>/dev/null; then
    kill -9 "${pid}" 2>/dev/null || true
  fi
  echo "stopped search server"
else
  echo "stale search server pid file"
fi

rm -f "${PID_FILE}"
