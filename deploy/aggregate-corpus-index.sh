#!/usr/bin/env bash
set -euo pipefail

DATA_ROOT="${AVENGINE_DATA_ROOT:-/data}"
WATCH=0
INTERVAL="${AVENGINE_AGGREGATE_INTERVAL_SECONDS:-300}"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --watch) WATCH=1; shift ;;
    --interval) INTERVAL="$2"; shift 2 ;;
    *) echo "usage: $0 [--watch] [--interval seconds]" >&2; exit 1 ;;
  esac
done

aggregate_once() {
  local corpus_root="${DATA_ROOT}/corpus-index"
  local generations_dir="${corpus_root}/generations"
  local generation="${generations_dir}/$(date -u +%Y%m%d%H%M%S)"
  local tmp_link="${corpus_root}/current.next"
  local counter=0

  mkdir -p "${generations_dir}" "${generation}"

  while IFS= read -r shard; do
    printf -v shard_name "%05d.sfi" "${counter}"
    cp "${shard}" "${generation}/${shard_name}"
    counter=$((counter + 1))
  done < <(find "${DATA_ROOT}/crawlers" -type f -name '*.sfi' | sort)

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
