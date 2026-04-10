#!/usr/bin/env bash
set -euo pipefail

DATA_ROOT="${AVENGINE_DATA_ROOT:-/data}"
SOURCE_DIR="${DATA_ROOT}/corpus-index/current"
PROFILE_ROOT="${DATA_ROOT}/corpus-profile"
GENERATIONS_DIR="${PROFILE_ROOT}/generations"
GENERATION="${GENERATIONS_DIR}/$(date -u +%Y%m%d%H%M%S)-$$"
TMP_LINK="${PROFILE_ROOT}/current.next"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --source)
      SOURCE_DIR="$2"
      shift 2
      ;;
    *)
      echo "usage: $0 [--source dir]" >&2
      exit 1
      ;;
  esac
done

if [[ ! -d "${SOURCE_DIR}" ]]; then
  echo "error: source corpus directory not found: ${SOURCE_DIR}" >&2
  exit 1
fi

mapfile -t SHARDS < <(find -L "${SOURCE_DIR}" -maxdepth 1 -type f -name '*.sfi' | sort)

if (( ${#SHARDS[@]} == 0 )); then
  echo "error: no .sfi shards found in ${SOURCE_DIR}" >&2
  exit 1
fi

mkdir -p "${GENERATIONS_DIR}" "${GENERATION}"

counter=0
for shard in "${SHARDS[@]}"; do
  printf -v shard_name "%05d.sfi" "${counter}"
  cp "${shard}" "${GENERATION}/${shard_name}"
  counter=$((counter + 1))
done

printf '%s\n' "${counter}" > "${GENERATION}/shard_count.txt"
ln -sfn "${GENERATION}" "${TMP_LINK}"
mv -Tf "${TMP_LINK}" "${PROFILE_ROOT}/current"

mapfile -t GENERATIONS < <(find "${GENERATIONS_DIR}" -mindepth 1 -maxdepth 1 -type d | sort)
if (( ${#GENERATIONS[@]} > 5 )); then
  for old in "${GENERATIONS[@]:0:${#GENERATIONS[@]}-5}"; do
    rm -rf "${old}"
  done
fi

echo "Promoted ${counter} shard(s) from ${SOURCE_DIR} to ${PROFILE_ROOT}/current"
