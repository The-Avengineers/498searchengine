#!/usr/bin/env bash
set -euo pipefail

DATA_ROOT="${AVENGINE_DATA_ROOT:-/data}"
BIN_ROOT="${AVENGINE_BIN_ROOT:-/avengine/bin}"
PORT="${1:-8080}"
INDEX_DIR="${2:-${DATA_ROOT}/corpus-index/current}"
MODE="${3:-foreground}"
PID_FILE="${DATA_ROOT}/pids/search-server.pid"
LOG_FILE="${DATA_ROOT}/logs/search-server.log"

mkdir -p "${DATA_ROOT}/pids" "${DATA_ROOT}/logs"

if [[ ! -d "${INDEX_DIR}" ]]; then
  echo "index directory not found: ${INDEX_DIR}" >&2
  exit 1
fi

AVENGINE_ROOT="${AVENGINE_ROOT:-/avengine}"
cd "${AVENGINE_ROOT}"

if [[ "${MODE}" == "--daemon" || "${MODE}" == "daemon" ]]; then
  if [[ -f "${PID_FILE}" ]]; then
    existing_pid="$(cat "${PID_FILE}")"
    if kill -0 "${existing_pid}" 2>/dev/null; then
      echo "search server already running (pid ${existing_pid})"
      exit 0
    fi
    rm -f "${PID_FILE}"
  fi

  "${BIN_ROOT}/search_server" "${PORT}" "${INDEX_DIR}" >> "${LOG_FILE}" 2>&1 &
  echo $! > "${PID_FILE}"
  echo "started search server on port ${PORT} (pid $(cat "${PID_FILE}"))"
  exit 0
fi

exec "${BIN_ROOT}/search_server" "${PORT}" "${INDEX_DIR}"
