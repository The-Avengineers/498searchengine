#!/usr/bin/env bash
set -euo pipefail

if [[ "${EUID}" -ne 0 ]]; then
  exec sudo -E bash "$0" "$@"
fi

if [[ -n "${AVENGINE_DEPLOY_ENV:-}" && -f "${AVENGINE_DEPLOY_ENV}" ]]; then
  set -a
  # shellcheck source=/dev/null
  source "${AVENGINE_DEPLOY_ENV}"
  set +a
fi

require_env() {
  local name="$1"
  if [[ -z "${!name:-}" ]]; then
    echo "missing required environment variable: ${name}" >&2
    exit 2
  fi
}

log() {
  printf '[avengine deploy] %s\n' "$*"
}

install_docker() {
  if command -v docker >/dev/null 2>&1 && docker compose version >/dev/null 2>&1; then
    return
  fi

  log "installing Docker Engine and Compose plugin"
  apt-get update
  DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    ca-certificates \
    curl \
    git \
    gnupg

  install -m 0755 -d /etc/apt/keyrings
  curl -fsSL https://download.docker.com/linux/ubuntu/gpg -o /etc/apt/keyrings/docker.asc
  chmod a+r /etc/apt/keyrings/docker.asc

  # shellcheck source=/dev/null
  source /etc/os-release
  cat > /etc/apt/sources.list.d/docker.list <<EOF
deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/docker.asc] https://download.docker.com/linux/ubuntu ${VERSION_CODENAME} stable
EOF

  apt-get update
  DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    docker-ce \
    docker-ce-cli \
    containerd.io \
    docker-buildx-plugin \
    docker-compose-plugin

  if command -v systemctl >/dev/null 2>&1; then
    systemctl enable --now docker
  fi
}

write_runtime_env() {
  local env_file="$1"

  install -d -m 0755 "$(dirname "${env_file}")"
  cat > "${env_file}" <<EOF
AVENGINE_DOMAIN=${AVENGINE_DOMAIN}
AVENGINE_PUBLIC_HTTP_PORT=${AVENGINE_PUBLIC_HTTP_PORT}
AVENGINE_PUBLIC_HTTPS_PORT=${AVENGINE_PUBLIC_HTTPS_PORT}
AVENGINE_SSH_PORT=${AVENGINE_SSH_PORT}
AVENGINE_HTTP_PORT=${AVENGINE_HTTP_PORT}
AVENGINE_HOST_DATA_ROOT=${AVENGINE_HOST_DATA_ROOT}
AVENGINE_SWARM_SIZE=${AVENGINE_SWARM_SIZE}
AVENGINE_CRAWLER_WORKERS=${AVENGINE_CRAWLER_WORKERS}
AVENGINE_AUTO_SWARM=${AVENGINE_AUTO_SWARM}
AVENGINE_AUTO_AGGREGATE=${AVENGINE_AUTO_AGGREGATE}
AVENGINE_AGGREGATE_INTERVAL_SECONDS=${AVENGINE_AGGREGATE_INTERVAL_SECONDS}
AVENGINE_MAX_DATA_BYTES=${AVENGINE_MAX_DATA_BYTES}
AVENGINE_MEMORY_LIMIT=${AVENGINE_MEMORY_LIMIT}
EOF
  chmod 600 "${env_file}"
}

install_authorized_keys() {
  local release_dir="$1"
  local target="${release_dir}/infra/deploy/authorized_keys"

  if [[ -n "${AVENGINE_SSH_PUBKEY_FILE:-}" && -s "${AVENGINE_SSH_PUBKEY_FILE}" ]]; then
    install -m 0600 "${AVENGINE_SSH_PUBKEY_FILE}" "${target}"
    return
  fi

  if [[ -n "${AVENGINE_SSH_PUBKEY:-}" ]]; then
    printf '%s\n' "${AVENGINE_SSH_PUBKEY}" > "${target}"
    chmod 600 "${target}"
    return
  fi

  echo "missing AVENGINE_SSH_PUBKEY_FILE or AVENGINE_SSH_PUBKEY" >&2
  exit 2
}

prepare_data_root() {
  local data_root="$1"
  local data_uid="$2"
  local data_gid="$3"
  local dirs=(
    "${data_root}"
    "${data_root}/logs"
    "${data_root}/pids"
    "${data_root}/crawlers"
    "${data_root}/corpus-index/generations"
    "${data_root}/corpus-profile/generations"
    "${data_root}/profiling"
  )

  install -d -m 0775 "${dirs[@]}"
  if [[ "${AVENGINE_REPAIR_DATA_OWNERSHIP}" != "0" ]]; then
    chown -R "${data_uid}:${data_gid}" "${data_root}"
  fi
}

extract_release() {
  local archive="$1"
  local release_dir="$2"
  local tmp_dir="${release_dir}.tmp"

  rm -rf "${tmp_dir}"
  install -d -m 0755 "${tmp_dir}"
  tar -xzf "${archive}" -C "${tmp_dir}"

  rm -rf "${release_dir}"
  mv "${tmp_dir}" "${release_dir}"
}

post_deploy_runtime_check() {
  local repo_dir="$1"
  local compose_args=(
    -p avengine
    --env-file "${AVENGINE_RUNTIME_ENV_FILE}"
    -f "${repo_dir}/infra/deploy/docker-compose.avengine.yml"
    -f "${repo_dir}/infra/deploy/docker-compose.caddy.yml"
  )

  "${repo_dir}/deploy/wait-runtime-ready.sh" "${AVENGINE_READY_TIMEOUT_SECONDS}"

  if ! docker exec -u engine avengine-runtime /avengine/bin/aggregate-corpus-index.sh; then
    log "aggregate pass did not complete; crawler data may not be ready yet"
  fi

  if docker exec avengine-runtime test -d /data/corpus-index/current; then
    if ! docker exec -u engine avengine-runtime \
      /avengine/bin/start-search-server.sh 8080 /data/corpus-index/current daemon; then
      log "search server did not start; public HTTPS will be ready after a corpus exists"
    fi
  else
    log "no current corpus index yet; skipping search server start"
  fi

  if [[ "${AVENGINE_ENABLE_LIVE_PROFILING}" != "0" ]]; then
    compose_args+=(-f "${repo_dir}/infra/deploy/docker-compose.profiling.yml")
  fi

  docker compose "${compose_args[@]}" ps
}

require_env AVENGINE_RELEASE_ARCHIVE
require_env AVENGINE_RELEASE_REF

AVENGINE_INSTALL_ROOT="${AVENGINE_INSTALL_ROOT:-/opt/avengine}"
AVENGINE_DOMAIN="${AVENGINE_DOMAIN:-theavengine.com}"
AVENGINE_PUBLIC_HTTP_PORT="${AVENGINE_PUBLIC_HTTP_PORT:-80}"
AVENGINE_PUBLIC_HTTPS_PORT="${AVENGINE_PUBLIC_HTTPS_PORT:-443}"
AVENGINE_SSH_PORT="${AVENGINE_SSH_PORT:-22222}"
AVENGINE_HTTP_PORT="${AVENGINE_HTTP_PORT:-127.0.0.1:8080}"
AVENGINE_HOST_DATA_ROOT="${AVENGINE_HOST_DATA_ROOT:-/mnt/data1/avengine/data}"
AVENGINE_SWARM_SIZE="${AVENGINE_SWARM_SIZE:-4}"
AVENGINE_CRAWLER_WORKERS="${AVENGINE_CRAWLER_WORKERS:-32}"
AVENGINE_AUTO_SWARM="${AVENGINE_AUTO_SWARM:-1}"
AVENGINE_AUTO_AGGREGATE="${AVENGINE_AUTO_AGGREGATE:-1}"
AVENGINE_AGGREGATE_INTERVAL_SECONDS="${AVENGINE_AGGREGATE_INTERVAL_SECONDS:-300}"
AVENGINE_MAX_DATA_BYTES="${AVENGINE_MAX_DATA_BYTES:-137438953472}"
AVENGINE_MEMORY_LIMIT="${AVENGINE_MEMORY_LIMIT:-12g}"
AVENGINE_DATA_UID="${AVENGINE_DATA_UID:-1000}"
AVENGINE_DATA_GID="${AVENGINE_DATA_GID:-1000}"
AVENGINE_REPAIR_DATA_OWNERSHIP="${AVENGINE_REPAIR_DATA_OWNERSHIP:-1}"
AVENGINE_ENABLE_LIVE_PROFILING="${AVENGINE_ENABLE_LIVE_PROFILING:-1}"
AVENGINE_READY_TIMEOUT_SECONDS="${AVENGINE_READY_TIMEOUT_SECONDS:-180}"
AVENGINE_RUNTIME_ENV_FILE="${AVENGINE_RUNTIME_ENV_FILE:-${AVENGINE_INSTALL_ROOT}/runtime.env}"
AVENGINE_SSH_PUBKEY_FILE="${AVENGINE_SSH_PUBKEY_FILE:-/tmp/avengine-authorized_keys}"

release_ref_safe="$(printf '%s' "${AVENGINE_RELEASE_REF}" | tr -c 'A-Za-z0-9._-' '-')"
release_dir="${AVENGINE_INSTALL_ROOT}/releases/${release_ref_safe}"
current_link="${AVENGINE_INSTALL_ROOT}/current"

install_docker
install -d -m 0755 "${AVENGINE_INSTALL_ROOT}/releases"
prepare_data_root "${AVENGINE_HOST_DATA_ROOT}" "${AVENGINE_DATA_UID}" "${AVENGINE_DATA_GID}"

log "extracting release ${AVENGINE_RELEASE_REF}"
extract_release "${AVENGINE_RELEASE_ARCHIVE}" "${release_dir}"
install_authorized_keys "${release_dir}"
write_runtime_env "${AVENGINE_RUNTIME_ENV_FILE}"

ln -sfn "${release_dir}" "${current_link}.next"
mv -Tf "${current_link}.next" "${current_link}"

compose_args=(
  -p avengine
  --env-file "${AVENGINE_RUNTIME_ENV_FILE}"
  -f "${current_link}/infra/deploy/docker-compose.avengine.yml"
  -f "${current_link}/infra/deploy/docker-compose.caddy.yml"
)
if [[ "${AVENGINE_ENABLE_LIVE_PROFILING}" != "0" ]]; then
  compose_args+=(-f "${current_link}/infra/deploy/docker-compose.profiling.yml")
fi

log "starting Docker Compose stack"
docker compose "${compose_args[@]}" up -d --build --remove-orphans

post_deploy_runtime_check "${current_link}"
log "deployment complete for https://${AVENGINE_DOMAIN}"
