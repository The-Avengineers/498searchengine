#!/usr/bin/env bash
set -euo pipefail

DATA_ROOT="${AVENGINE_DATA_ROOT:-/data}"
BIN_ROOT="${AVENGINE_BIN_ROOT:-/avengine/bin}"
PORT="${AVENGINE_SEARCH_PORT:-8080}"
PRIMARY_INDEX="${DATA_ROOT}/corpus-index/current"
FALLBACK_INDEX="${DATA_ROOT}/empty-index"
PID_FILE="${DATA_ROOT}/pids/search-server.pid"

mkdir -p "${DATA_ROOT}/pids" "${DATA_ROOT}/logs" "${FALLBACK_INDEX}"

cleanup() {
  "${BIN_ROOT}/stop-search-server.sh" >/dev/null 2>&1 || true
}

trap cleanup TERM INT

index_dir="${FALLBACK_INDEX}"
if [[ -d "${PRIMARY_INDEX}" ]]; then
  index_dir="${PRIMARY_INDEX}"
fi

"${BIN_ROOT}/start-search-server.sh" "${PORT}" "${index_dir}" daemon

if [[ ! -f "${PID_FILE}" ]]; then
  echo "search server did not write pid file ${PID_FILE}" >&2
  exit 1
fi

pid="$(cat "${PID_FILE}")"
while kill -0 "${pid}" 2>/dev/null; do
  sleep 5
done

exit 1
