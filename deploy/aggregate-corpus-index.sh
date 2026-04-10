#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/data-limit.sh"

DATA_ROOT="${AVENGINE_DATA_ROOT:-/data}"
BIN_ROOT="${AVENGINE_BIN_ROOT:-/avengine/bin}"
WATCH=0
INTERVAL="${AVENGINE_AGGREGATE_INTERVAL_SECONDS:-300}"
USAGE_REFRESH_EVERY_SHARDS="${AVENGINE_MAX_DATA_REFRESH_EVERY_SHARDS:-16}"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --watch) WATCH=1; shift ;;
    --interval) INTERVAL="$2"; shift 2 ;;
    *) echo "usage: $0 [--watch] [--interval seconds]" >&2; exit 1 ;;
  esac
done

validate_data_limit_env

if ! [[ "${USAGE_REFRESH_EVERY_SHARDS}" =~ ^[0-9]+$ ]] || [[ "${USAGE_REFRESH_EVERY_SHARDS}" -lt 1 ]]; then
  echo "AVENGINE_MAX_DATA_REFRESH_EVERY_SHARDS must be a positive integer" >&2
  exit 1
fi

stop_crawling_for_data_limit() {
  local usage_bytes="$1"

  report_data_limit "${usage_bytes}" "aggregate stopped"
  "${BIN_ROOT}/stop-crawler-swarm.sh" --quiet >/dev/null 2>&1 || true
  "${BIN_ROOT}/stop-crawler-supervisor.sh" >/dev/null 2>&1 || true
}

aggregate_once() {
  local corpus_root="${DATA_ROOT}/corpus-index"
  local generations_dir="${corpus_root}/generations"
  local generation="${generations_dir}/$(date -u +%Y%m%d%H%M%S)"
  local tmp_link="${corpus_root}/current.next"
  local counter=0
  local usage_bytes=0
  local shard_bytes=0

  usage_bytes="$(data_root_usage_bytes)"
  if data_limit_reached "${usage_bytes}"; then
    stop_crawling_for_data_limit "${usage_bytes}"
    return 75
  fi

  mkdir -p "${generations_dir}" "${generation}"

  while IFS= read -r shard; do
    shard_bytes="$(path_usage_bytes "${shard}")"
    if data_limit_would_be_reached "${usage_bytes}" "${shard_bytes}"; then
      stop_crawling_for_data_limit "$((usage_bytes + shard_bytes))"
      rm -rf "${generation}"
      return 75
    fi

    printf -v shard_name "%05d.sfi" "${counter}"
    if ! cp "${shard}" "${generation}/${shard_name}"; then
      rm -rf "${generation}"
      return 1
    fi

    usage_bytes=$((usage_bytes + shard_bytes))
    counter=$((counter + 1))

    if (( counter % USAGE_REFRESH_EVERY_SHARDS == 0 )); then
      usage_bytes="$(data_root_usage_bytes)"
      if data_limit_reached "${usage_bytes}"; then
        stop_crawling_for_data_limit "${usage_bytes}"
        rm -rf "${generation}"
        return 75
      fi
    fi
  done < <(find "${DATA_ROOT}/crawlers" -type f -name '*.sfi' | sort)

  usage_bytes="$(data_root_usage_bytes)"
  if data_limit_reached "${usage_bytes}"; then
    stop_crawling_for_data_limit "${usage_bytes}"
    rm -rf "${generation}"
    return 75
  fi

  printf '%s\n' "${counter}" > "${generation}/shard_count.txt"
  ln -sfn "${generation}" "${tmp_link}"
  mv -Tf "${tmp_link}" "${corpus_root}/current"

  mapfile -t generations < <(find "${generations_dir}" -mindepth 1 -maxdepth 1 -type d | sort)
  if (( ${#generations[@]} > 5 )); then
    for old in "${generations[@]:0:${#generations[@]}-5}"; do
      rm -rf "${old}"
    done
  fi
}

if [[ "${WATCH}" -eq 1 ]]; then
  while true; do
    aggregate_once
    sleep "${INTERVAL}"
  done
else
  aggregate_once
fi
