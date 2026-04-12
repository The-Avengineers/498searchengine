#!/usr/bin/env bash
set -euo pipefail

CONTAINER_NAME="${AVENGINE_CONTAINER_NAME:-avengine-runtime}"
TIMEOUT_SECONDS="${1:-120}"
INTERVAL_SECONDS="${AVENGINE_READY_INTERVAL_SECONDS:-3}"

if ! [[ "${TIMEOUT_SECONDS}" =~ ^[0-9]+$ ]] || (( TIMEOUT_SECONDS < 1 )); then
  echo "usage: $0 [timeout-seconds]" >&2
  exit 2
fi

deadline=$((SECONDS + TIMEOUT_SECONDS))
last_status=""

while (( SECONDS < deadline )); do
  if ! docker container inspect "${CONTAINER_NAME}" >/dev/null 2>&1; then
    last_status="missing"
    sleep "${INTERVAL_SECONDS}"
    continue
  fi

  state="$(docker container inspect -f '{{.State.Status}}' "${CONTAINER_NAME}" 2>/dev/null || true)"
  running="$(docker container inspect -f '{{.State.Running}}' "${CONTAINER_NAME}" 2>/dev/null || true)"
  restarting="$(docker container inspect -f '{{.State.Restarting}}' "${CONTAINER_NAME}" 2>/dev/null || true)"
  last_status="state=${state} running=${running} restarting=${restarting}"

  if [[ "${running}" == "true" && "${restarting}" != "true" ]]; then
    if docker exec "${CONTAINER_NAME}" /usr/local/bin/av-status > /tmp/avengine-status.txt 2>&1; then
      if grep -q "crawler supervisor: running" /tmp/avengine-status.txt &&
         grep -q "aggregate watcher: running" /tmp/avengine-status.txt; then
        cat /tmp/avengine-status.txt
        exit 0
      fi
      last_status="$(cat /tmp/avengine-status.txt)"
    else
      last_status="$(cat /tmp/avengine-status.txt 2>/dev/null || true)"
    fi
  fi

  sleep "${INTERVAL_SECONDS}"
done

echo "::error::${CONTAINER_NAME} was not ready after ${TIMEOUT_SECONDS}s" >&2
echo "${last_status}" >&2
echo "--- recent container logs ---" >&2
docker logs --tail 120 "${CONTAINER_NAME}" >&2 || true
exit 1
