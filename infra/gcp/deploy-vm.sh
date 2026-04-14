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

install_caddy() {
  if command -v caddy >/dev/null 2>&1; then
    return
  fi

  if DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends caddy; then
    return
  fi

  log "Ubuntu caddy package unavailable; installing from Caddy upstream apt repository"
  DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    debian-keyring \
    debian-archive-keyring \
    apt-transport-https
  rm -f /usr/share/keyrings/caddy-stable-archive-keyring.gpg
  curl -1sLf 'https://dl.cloudsmith.io/public/caddy/stable/gpg.key' \
    | gpg --dearmor -o /usr/share/keyrings/caddy-stable-archive-keyring.gpg
  curl -1sLf 'https://dl.cloudsmith.io/public/caddy/stable/debian.deb.txt' \
    > /etc/apt/sources.list.d/caddy-stable.list
  apt-get update
  DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends caddy
}

install_packages() {
  log "installing native runtime dependencies"
  apt-get update
  DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    binutils \
    build-essential \
    ca-certificates \
    cmake \
    curl \
    git \
    gnupg \
    libssl-dev \
    procps \
    psmisc \
    python3 \
    sudo \
    zlib1g-dev
  DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends bpftrace \
    || log "bpftrace unavailable; live profiling commands may be limited"
  install_caddy
}

create_engine_user() {
  if ! id -u engine >/dev/null 2>&1; then
    useradd -m -s /bin/bash engine
  fi

  install -d -o engine -g engine -m 700 /home/engine/.ssh

  if [[ -n "${AVENGINE_SSH_PUBKEY_FILE:-}" && -s "${AVENGINE_SSH_PUBKEY_FILE}" ]]; then
    install -o engine -g engine -m 600 "${AVENGINE_SSH_PUBKEY_FILE}" /home/engine/.ssh/authorized_keys
  elif [[ -n "${AVENGINE_SSH_PUBKEY:-}" ]]; then
    printf '%s\n' "${AVENGINE_SSH_PUBKEY}" > /home/engine/.ssh/authorized_keys
    chown engine:engine /home/engine/.ssh/authorized_keys
    chmod 600 /home/engine/.ssh/authorized_keys
  else
    echo "missing AVENGINE_SSH_PUBKEY_FILE or AVENGINE_SSH_PUBKEY" >&2
    exit 2
  fi

  printf 'engine ALL=(root) NOPASSWD: /avengine/bin/avengine-live-profile.sh, /avengine/bin/avengine-live-profile.sh *\n' \
    > /etc/sudoers.d/avengine-live-profile
  chmod 440 /etc/sudoers.d/avengine-live-profile
}

write_runtime_env() {
  local env_file="$1"

  install -d -m 0755 "$(dirname "${env_file}")"
  cat > "${env_file}" <<EOF
AVENGINE_ROOT=/avengine
AVENGINE_BIN_ROOT=/avengine/bin
AVENGINE_DATA_ROOT=${AVENGINE_DATA_ROOT}
AVENGINE_CONFIG_ROOT=/config
AVENGINE_SWARM_SIZE=${AVENGINE_SWARM_SIZE}
AVENGINE_CRAWLER_WORKERS=${AVENGINE_CRAWLER_WORKERS}
AVENGINE_AGGREGATE_INTERVAL_SECONDS=${AVENGINE_AGGREGATE_INTERVAL_SECONDS}
AVENGINE_MAX_DATA_BYTES=${AVENGINE_MAX_DATA_BYTES}
AVENGINE_SEARCH_PORT=${AVENGINE_SEARCH_PORT}
EOF
  chmod 644 "${env_file}"
}

prepare_data_root() {
  local data_root="$1"
  local dirs=(
    "${data_root}"
    "${data_root}/empty-index"
    "${data_root}/logs"
    "${data_root}/pids"
    "${data_root}/crawlers"
    "${data_root}/corpus-index/generations"
    "${data_root}/corpus-profile/generations"
    "${data_root}/profiling"
  )

  install -d -m 0775 "${dirs[@]}"
  if [[ "${AVENGINE_REPAIR_DATA_OWNERSHIP}" != "0" ]]; then
    chown -R engine:engine "${data_root}"
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

install_release_runtime() {
  local release_dir="$1"
  local build_dir="${release_dir}/build"

  log "building native AVEngine binaries"
  cmake -S "${release_dir}" -B "${build_dir}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=OFF
  cmake --build "${build_dir}" --target \
    crawler_main \
    crawler_index_profile \
    search_server \
    index_builder \
    anchor_processor \
    -j"$(nproc)"

  install -d -m 0755 "${release_dir}/bin" "${release_dir}/config"
  install -m 0755 \
    "${build_dir}/crawler_main" \
    "${build_dir}/crawler_index_profile" \
    "${build_dir}/search_server" \
    "${build_dir}/index_builder" \
    "${build_dir}/anchor_processor" \
    "${release_dir}/bin/"
  install -m 0755 "${release_dir}"/deploy/*.sh "${release_dir}/bin/"
  install -m 0644 \
    "${release_dir}/seed_list.txt" \
    "${release_dir}/banned_urls.txt" \
    "${release_dir}/banned_words.txt" \
    "${release_dir}/config/"
}

refresh_runtime_symlinks() {
  local current_link="$1"

  for link_path in /avengine /config /logs /crawlers /corpus; do
    if [[ -e "${link_path}" && ! -L "${link_path}" ]]; then
      echo "${link_path} exists and is not a symlink; refusing to replace it" >&2
      exit 2
    fi
  done

  ln -sfn "${current_link}" /avengine
  ln -sfn /avengine/config /config
  ln -sfn "${AVENGINE_DATA_ROOT}/logs" /logs
  ln -sfn "${AVENGINE_DATA_ROOT}/crawlers" /crawlers
  ln -sfn "${AVENGINE_DATA_ROOT}/corpus-index/current" /corpus
}

write_caddyfile() {
  cat > /etc/caddy/Caddyfile <<EOF
www.${AVENGINE_DOMAIN} {
	redir https://${AVENGINE_DOMAIN}{uri} permanent
}

${AVENGINE_DOMAIN} {
	reverse_proxy 127.0.0.1:${AVENGINE_SEARCH_PORT}
}
EOF
}

install_systemd_units() {
  local release_dir="$1"

  install -m 0755 "${release_dir}/infra/gcp/avengine-search-service.sh" \
    /usr/local/bin/avengine-search-service.sh
  install -m 0644 "${release_dir}/infra/gcp/systemd/"*.service \
    /etc/systemd/system/
  systemctl daemon-reload
}

stop_services() {
  if command -v systemctl >/dev/null 2>&1; then
    systemctl stop avengine-search.service avengine-aggregate.service avengine-crawler.service >/dev/null 2>&1 || true
  fi
  if [[ -x /avengine/bin/avengine-stop.sh ]]; then
    sudo -u engine env \
      AVENGINE_BIN_ROOT=/avengine/bin \
      AVENGINE_DATA_ROOT="${AVENGINE_DATA_ROOT}" \
      /avengine/bin/avengine-stop.sh >/dev/null 2>&1 || true
  fi
}

run_as_engine() {
  sudo -u engine env \
    AVENGINE_ROOT=/avengine \
    AVENGINE_BIN_ROOT=/avengine/bin \
    AVENGINE_DATA_ROOT="${AVENGINE_DATA_ROOT}" \
    AVENGINE_CONFIG_ROOT=/config \
    AVENGINE_SWARM_SIZE="${AVENGINE_SWARM_SIZE}" \
    AVENGINE_CRAWLER_WORKERS="${AVENGINE_CRAWLER_WORKERS}" \
    AVENGINE_AGGREGATE_INTERVAL_SECONDS="${AVENGINE_AGGREGATE_INTERVAL_SECONDS}" \
    AVENGINE_MAX_DATA_BYTES="${AVENGINE_MAX_DATA_BYTES}" \
    "$@"
}

wait_managed_services() {
  local deadline=$((SECONDS + AVENGINE_READY_TIMEOUT_SECONDS))
  local status_output=""

  while (( SECONDS < deadline )); do
    status_output="$(run_as_engine /avengine/bin/avengine-status.sh 2>&1 || true)"
    if grep -q "crawler supervisor: running" <<<"${status_output}" &&
       grep -q "aggregate watcher: running" <<<"${status_output}"; then
      printf '%s\n' "${status_output}"
      return
    fi
    sleep 3
  done

  echo "AVEngine managed services were not ready after ${AVENGINE_READY_TIMEOUT_SECONDS}s" >&2
  printf '%s\n' "${status_output}" >&2
  exit 1
}

start_services() {
  systemctl enable caddy avengine-crawler.service avengine-aggregate.service avengine-search.service
  systemctl restart caddy
  systemctl restart avengine-crawler.service avengine-aggregate.service
  wait_managed_services

  if ! run_as_engine /avengine/bin/aggregate-corpus-index.sh; then
    log "aggregate pass did not complete; crawler data may not be ready yet"
  fi

  systemctl restart avengine-search.service
  systemctl --no-pager --full status \
    caddy \
    avengine-crawler.service \
    avengine-aggregate.service \
    avengine-search.service || true
}

require_env AVENGINE_RELEASE_ARCHIVE
require_env AVENGINE_RELEASE_REF

AVENGINE_INSTALL_ROOT="${AVENGINE_INSTALL_ROOT:-/opt/avengine}"
AVENGINE_DOMAIN="${AVENGINE_DOMAIN:-theavengine.com}"
AVENGINE_DATA_ROOT="${AVENGINE_DATA_ROOT:-/data}"
AVENGINE_SWARM_SIZE="${AVENGINE_SWARM_SIZE:-4}"
AVENGINE_CRAWLER_WORKERS="${AVENGINE_CRAWLER_WORKERS:-32}"
AVENGINE_AGGREGATE_INTERVAL_SECONDS="${AVENGINE_AGGREGATE_INTERVAL_SECONDS:-300}"
AVENGINE_MAX_DATA_BYTES="${AVENGINE_MAX_DATA_BYTES:-137438953472}"
AVENGINE_REPAIR_DATA_OWNERSHIP="${AVENGINE_REPAIR_DATA_OWNERSHIP:-1}"
AVENGINE_READY_TIMEOUT_SECONDS="${AVENGINE_READY_TIMEOUT_SECONDS:-180}"
AVENGINE_SEARCH_PORT="${AVENGINE_SEARCH_PORT:-8080}"
AVENGINE_RUNTIME_ENV_FILE="${AVENGINE_RUNTIME_ENV_FILE:-${AVENGINE_INSTALL_ROOT}/runtime.env}"
AVENGINE_SSH_PUBKEY_FILE="${AVENGINE_SSH_PUBKEY_FILE:-/tmp/avengine-authorized_keys}"

release_ref_safe="$(printf '%s' "${AVENGINE_RELEASE_REF}" | tr -c 'A-Za-z0-9._-' '-')"
release_dir="${AVENGINE_INSTALL_ROOT}/releases/${release_ref_safe}"
current_link="${AVENGINE_INSTALL_ROOT}/current"

install_packages
create_engine_user
install -d -m 0755 "${AVENGINE_INSTALL_ROOT}/releases"
prepare_data_root "${AVENGINE_DATA_ROOT}"
stop_services

log "extracting release ${AVENGINE_RELEASE_REF}"
extract_release "${AVENGINE_RELEASE_ARCHIVE}" "${release_dir}"
install_release_runtime "${release_dir}"
write_runtime_env "${AVENGINE_RUNTIME_ENV_FILE}"

ln -sfn "${release_dir}" "${current_link}.next"
mv -Tf "${current_link}.next" "${current_link}"
refresh_runtime_symlinks "${current_link}"
write_caddyfile
install_systemd_units "${current_link}"
start_services

log "deployment complete for https://${AVENGINE_DOMAIN}"
