#!/usr/bin/env bash

DATA_ROOT="${AVENGINE_DATA_ROOT:-/data}"
MAX_DATA_BYTES="${AVENGINE_MAX_DATA_BYTES:-51539607552}"

validate_data_limit_env() {
  if ! [[ "${MAX_DATA_BYTES}" =~ ^[0-9]+$ ]]; then
    echo "AVENGINE_MAX_DATA_BYTES must be a non-negative integer" >&2
    exit 1
  fi
}

data_limit_enabled() {
  [[ "${MAX_DATA_BYTES}" =~ ^[0-9]+$ ]] && (( MAX_DATA_BYTES > 0 ))
}

format_bytes() {
  if command -v numfmt >/dev/null 2>&1; then
    numfmt --to=iec-i --suffix=B "$1"
  else
    printf '%s B\n' "$1"
  fi
}

data_root_usage_bytes() {
  du -sb "${DATA_ROOT}" | cut -f1
}

path_usage_bytes() {
  du -sk "$1" | awk '{print $1 * 1024}'
}

data_limit_reached() {
  local usage_bytes="${1:-$(data_root_usage_bytes)}"

  data_limit_enabled && (( usage_bytes >= MAX_DATA_BYTES ))
}

data_limit_would_be_reached() {
  local usage_bytes="$1"
  local extra_bytes="$2"

  data_limit_enabled && (( usage_bytes + extra_bytes >= MAX_DATA_BYTES ))
}

report_data_limit() {
  local usage_bytes="$1"
  local label="${2:-data limit reached}"

  echo "${label}: ${DATA_ROOT} usage $(format_bytes "${usage_bytes}") reached limit $(format_bytes "${MAX_DATA_BYTES}")" >&2
}
