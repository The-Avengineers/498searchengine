#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
COMPOSE_DIR="${SCRIPT_DIR}/infra/deploy"
COMPOSE_FILE="docker-compose.avengine.yml"
COMPOSE_PROFILING_FILE="docker-compose.profiling.yml"
SERVICE_NAME="avengine"
CONTAINER_NAME="avengine-runtime"
SSH_PORT="${AVENGINE_SSH_PORT:-2222}"
HOST_DATA_ROOT="${AVENGINE_HOST_DATA_ROOT:-/mnt/data1/avengine/data}"
ENGINE_UID="${AVENGINE_DATA_UID:-1000}"
ENGINE_GID="${AVENGINE_DATA_GID:-1000}"
export DOCKER_CONTEXT="${DOCKER_CONTEXT:-default}"

usage() {
  cat <<'EOF'
usage: ./run-container.sh <command>

commands:
  up       Build and start the runtime container
  down     Stop and remove the runtime container
  logs     Follow container logs
  ssh      SSH into the running container as engine
  status   Show compose status
  config   Show resolved compose config

Environment:
  DOCKER_CONTEXT=default  use the host Docker Engine so /mnt/data1 bind mounts work
  AVENGINE_HOST_DATA_ROOT=/mnt/data1/avengine/data
  AVENGINE_DATA_UID=1000 AVENGINE_DATA_GID=1000
  AVENGINE_REPAIR_DATA_OWNERSHIP=0  skip host data chown repair
  AVENGINE_ENABLE_LIVE_PROFILING=1  include profiling privileges and tracefs mounts
EOF
}

ensure_pubkey() {
  if [[ -n "${AVENGINE_SSH_PUBKEY:-}" ]]; then
    return
  fi

  local key_path=""
  for candidate in "${HOME}/.ssh/id_ed25519.pub" "${HOME}/.ssh/id_rsa.pub"; do
    if [[ -f "${candidate}" ]]; then
      key_path="${candidate}"
      break
    fi
  done

  if [[ -z "${key_path}" ]]; then
    echo "Set AVENGINE_SSH_PUBKEY or create ~/.ssh/id_ed25519.pub (or ~/.ssh/id_rsa.pub)." >&2
    exit 1
  fi

  export AVENGINE_SSH_PUBKEY
  AVENGINE_SSH_PUBKEY="$(<"${key_path}")"
}

ensure_data_root() {
  local dirs=(
    "${HOST_DATA_ROOT}"
    "${HOST_DATA_ROOT}/logs"
    "${HOST_DATA_ROOT}/pids"
    "${HOST_DATA_ROOT}/crawlers"
    "${HOST_DATA_ROOT}/corpus-index/generations"
    "${HOST_DATA_ROOT}/corpus-profile/generations"
    "${HOST_DATA_ROOT}/profiling"
  )

  if ! install -d -m 775 "${dirs[@]}" 2>/dev/null; then
    sudo -n install -d -m 775 "${dirs[@]}"
  fi

  if [[ "${AVENGINE_REPAIR_DATA_OWNERSHIP:-1}" == "0" ]]; then
    return
  fi

  if [[ "$(id -u)" -eq 0 ]]; then
    chown -R "${ENGINE_UID}:${ENGINE_GID}" "${HOST_DATA_ROOT}"
  elif command -v sudo >/dev/null 2>&1 && sudo -n true 2>/dev/null; then
    sudo chown -R "${ENGINE_UID}:${ENGINE_GID}" "${HOST_DATA_ROOT}"
  else
    echo "warning: could not repair ${HOST_DATA_ROOT} ownership without passwordless sudo" >&2
    echo "warning: run: sudo chown -R ${ENGINE_UID}:${ENGINE_GID} ${HOST_DATA_ROOT}" >&2
  fi
}

compose() {
  (
    cd "${COMPOSE_DIR}"
    local compose_args=(-f "${COMPOSE_FILE}")
    if [[ "${AVENGINE_ENABLE_LIVE_PROFILING:-1}" != "0" ]]; then
      compose_args+=(-f "${COMPOSE_PROFILING_FILE}")
    fi
    docker compose "${compose_args[@]}" "$@"
  )
}

cmd="${1:-}"

case "${cmd}" in
  up)
    ensure_pubkey
    ensure_data_root
    compose up --build -d
    compose ps
    ;;
  down)
    compose down
    ;;
  logs)
    compose logs -f "${SERVICE_NAME}"
    ;;
  ssh)
    ensure_pubkey
    exec ssh -p "${SSH_PORT}" engine@localhost
    ;;
  status)
    compose ps
    ;;
  config)
    compose config
    ;;
  ""|-h|--help|help)
    usage
    ;;
  *)
    echo "Unknown command: ${cmd}" >&2
    usage >&2
    exit 1
    ;;
esac
