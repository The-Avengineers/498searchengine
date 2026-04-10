#!/usr/bin/env bash
set -euo pipefail

BIN_ROOT="${AVENGINE_BIN_ROOT:-/avengine/bin}"
INTERVAL="${1:-5}"

if ! [[ "${INTERVAL}" =~ ^[0-9]+$ ]] || [[ "${INTERVAL}" -lt 1 ]]; then
  echo "usage: av-watch [seconds]" >&2
  exit 1
fi

while true; do
  clear
  date
  echo
  "${BIN_ROOT}/avengine-status.sh"
  echo
  "${BIN_ROOT}/avengine-stats.sh"
  sleep "${INTERVAL}"
done
