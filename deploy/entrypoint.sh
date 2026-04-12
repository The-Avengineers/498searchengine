#!/usr/bin/env bash
set -euo pipefail

DATA_ROOT="${AVENGINE_DATA_ROOT:-/data}"
BIN_ROOT="${AVENGINE_BIN_ROOT:-/avengine/bin}"
AUTHORIZED_KEYS_FILE="/home/engine/.ssh/authorized_keys"
MOUNTED_KEYS_FILE="/run/avengine/authorized_keys"

warn() {
  echo "entrypoint warning: $*" >&2
}

try_chown_engine() {
  local path="$1"
  if [[ -e "${path}" ]] && ! chown engine:engine "${path}" 2>/tmp/avengine-chown.err; then
    warn "could not chown ${path} to engine:engine ($(cat /tmp/avengine-chown.err)); continuing"
  fi
}

require_engine_writable_dir() {
  local path="$1"
  local quoted_path
  quoted_path="$(printf "%q" "${path}")"
  if ! su -s /bin/bash engine -c "test -d ${quoted_path} && touch ${quoted_path}/.avengine-write-test && rm -f ${quoted_path}/.avengine-write-test"; then
    echo "entrypoint error: ${path} must be writable by the engine user inside the container" >&2
    echo "entrypoint error: fix the host bind mount permissions or set AVENGINE_HOST_DATA_ROOT to a writable Docker-shared path" >&2
    exit 1
  fi
}

install -d -o engine -g engine -m 700 /home/engine/.ssh
: > "${AUTHORIZED_KEYS_FILE}"

if [[ -f "${MOUNTED_KEYS_FILE}" ]]; then
  grep -v '^[[:space:]]*$' "${MOUNTED_KEYS_FILE}" >> "${AUTHORIZED_KEYS_FILE}" || true
fi

if [[ -n "${AVENGINE_SSH_PUBKEY:-}" ]]; then
  printf '%s\n' "${AVENGINE_SSH_PUBKEY}" >> "${AUTHORIZED_KEYS_FILE}"
fi

sort -u "${AUTHORIZED_KEYS_FILE}" -o "${AUTHORIZED_KEYS_FILE}"
chown engine:engine "${AUTHORIZED_KEYS_FILE}"
chmod 600 "${AUTHORIZED_KEYS_FILE}"

mkdir -p "${DATA_ROOT}" "${DATA_ROOT}/logs" "${DATA_ROOT}/pids" "${DATA_ROOT}/crawlers" \
  "${DATA_ROOT}/corpus-index" "${DATA_ROOT}/corpus-profile" "${DATA_ROOT}/profiling"
for dir in "${DATA_ROOT}" "${DATA_ROOT}/logs" "${DATA_ROOT}/pids" "${DATA_ROOT}/crawlers" \
  "${DATA_ROOT}/corpus-index" "${DATA_ROOT}/corpus-profile" "${DATA_ROOT}/profiling"; do
  try_chown_engine "${dir}"
  require_engine_writable_dir "${dir}"
done
rm -f "${DATA_ROOT}"/pids/*.pid || warn "could not remove stale pid files from ${DATA_ROOT}/pids"
chown -R engine:engine /avengine /home/engine/.ssh
chown engine:engine /run/avengine

# Harden SSH for key-based login only.
sed -i 's/^#*PasswordAuthentication .*/PasswordAuthentication no/' /etc/ssh/sshd_config
sed -i 's/^#*PermitRootLogin .*/PermitRootLogin no/' /etc/ssh/sshd_config
sed -i 's/^#*PubkeyAuthentication .*/PubkeyAuthentication yes/' /etc/ssh/sshd_config

if [[ "${AVENGINE_AUTO_SWARM:-1}" != "0" ]]; then
  su -s /bin/bash engine -c "${BIN_ROOT}/start-crawler-supervisor.sh"
fi

if [[ "${AVENGINE_AUTO_AGGREGATE:-1}" != "0" ]]; then
  su -s /bin/bash engine -c "${BIN_ROOT}/start-aggregate-watch.sh"
fi

exec /usr/sbin/sshd -D -e
