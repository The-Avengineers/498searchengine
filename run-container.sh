#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
COMPOSE_DIR="${SCRIPT_DIR}/infra/deploy"
COMPOSE_FILE="docker-compose.avengine.yml"
SERVICE_NAME="avengine"
CONTAINER_NAME="avengine-runtime"
SSH_PORT="${AVENGINE_SSH_PORT:-2222}"

usage() {
  cat <<'EOF'
usage: ./run-container.sh <command>

commands:
  up       Build and start the runtime container
  down     Stop and remove the runtime container
  logs     Follow container logs
  ssh      SSH into the running container as engine
  status   Show compose status
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

compose() {
  (
    cd "${COMPOSE_DIR}"
    docker compose -f "${COMPOSE_FILE}" "$@"
  )
}

cmd="${1:-}"

case "${cmd}" in
  up)
    ensure_pubkey
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
  ""|-h|--help|help)
    usage
    ;;
  *)
    echo "Unknown command: ${cmd}" >&2
    usage >&2
    exit 1
    ;;
esac
