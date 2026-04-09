#!/usr/bin/env bash
set -euo pipefail

DATA_ROOT="${AVENGINE_DATA_ROOT:-/data}"
CONFIG_ROOT="${AVENGINE_CONFIG_ROOT:-/config}"
BIN_ROOT="${AVENGINE_BIN_ROOT:-/avengine/bin}"
SWARM_SIZE="${AVENGINE_SWARM_SIZE:-4}"
CRAWLER_WORKERS="${AVENGINE_CRAWLER_WORKERS:-64}"
RESET=0

if [[ "${1:-}" == "--reset" ]]; then
  RESET=1
elif [[ -n "${1:-}" ]]; then
  echo "usage: $0 [--reset]" >&2
  exit 1
fi

if ! [[ "${SWARM_SIZE}" =~ ^[0-9]+$ ]] || [[ "${SWARM_SIZE}" -lt 1 ]]; then
  echo "AVENGINE_SWARM_SIZE must be a positive integer" >&2
  exit 1
fi

mkdir -p "${DATA_ROOT}/crawlers" "${DATA_ROOT}/logs" "${DATA_ROOT}/pids" \
  "${DATA_ROOT}/corpus-index/generations"

if [[ "${RESET}" -eq 1 ]]; then
  "${BIN_ROOT}/stop-crawler-supervisor.sh" || true
  "${BIN_ROOT}/stop-crawler-swarm.sh" --quiet || true
  rm -rf "${DATA_ROOT}/crawlers" "${DATA_ROOT}/corpus-index"
  mkdir -p "${DATA_ROOT}/crawlers" "${DATA_ROOT}/corpus-index/generations"
fi

if [[ ! -f "${CONFIG_ROOT}/seed_list.txt" ]]; then
  echo "missing seed list at ${CONFIG_ROOT}/seed_list.txt" >&2
  exit 1
fi

mapfile -t ALL_SEEDS < <(grep -v '^[[:space:]]*$' "${CONFIG_ROOT}/seed_list.txt")
if [[ "${#ALL_SEEDS[@]}" -eq 0 ]]; then
  echo "seed_list.txt is empty" >&2
  exit 1
fi

for ((worker_id = 0; worker_id < SWARM_SIZE; ++worker_id)); do
  worker_dir="${DATA_ROOT}/crawlers/${worker_id}"
  pid_file="${DATA_ROOT}/pids/crawler-${worker_id}.pid"
  log_file="${DATA_ROOT}/logs/crawler-${worker_id}.log"

  if [[ -f "${pid_file}" ]]; then
    existing_pid="$(cat "${pid_file}")"
    if kill -0 "${existing_pid}" 2>/dev/null; then
      continue
    fi
    rm -f "${pid_file}"
  fi

  mkdir -p "${worker_dir}"
  cp "${CONFIG_ROOT}/banned_urls.txt" "${worker_dir}/banned_urls.txt"
  cp "${CONFIG_ROOT}/banned_words.txt" "${worker_dir}/banned_words.txt"

  : > "${worker_dir}/seed_list.txt"
  for idx in "${!ALL_SEEDS[@]}"; do
    if (( idx % SWARM_SIZE == worker_id )); then
      printf '%s\n' "${ALL_SEEDS[idx]}" >> "${worker_dir}/seed_list.txt"
    fi
  done

  if [[ ! -s "${worker_dir}/seed_list.txt" ]]; then
    continue
  fi

  (
    cd "${worker_dir}"
    CRAWLER_WORKERS="${CRAWLER_WORKERS}" \
      "${BIN_ROOT}/crawler_main" >> "${log_file}" 2>&1 &
    echo $! > "${pid_file}"
  )
done
