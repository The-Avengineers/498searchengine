#!/usr/bin/env bash
set -euo pipefail

DATA_ROOT="${AVENGINE_DATA_ROOT:-/var/lib/avengine}"
QUIET=0

if [[ "${1:-}" == "--quiet" ]]; then
  QUIET=1
elif [[ -n "${1:-}" ]]; then
  echo "usage: $0 [--quiet]" >&2
  exit 1
fi

shopt -s nullglob
for pid_file in "${DATA_ROOT}"/pids/crawler-*.pid; do
  pid="$(cat "${pid_file}")"
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
  elif [[ "${QUIET}" -eq 0 ]]; then
    echo "stale pid file: ${pid_file}" >&2
  fi
  rm -f "${pid_file}"
done
