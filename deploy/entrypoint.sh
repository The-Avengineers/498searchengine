#!/usr/bin/env bash
set -euo pipefail

DATA_ROOT="${AVENGINE_DATA_ROOT:-/data}"
BIN_ROOT="${AVENGINE_BIN_ROOT:-/avengine/bin}"
AUTHORIZED_KEYS_FILE="/home/engine/.ssh/authorized_keys"
MOUNTED_KEYS_FILE="/run/avengine/authorized_keys"

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

mkdir -p "${DATA_ROOT}"
chown -R engine:engine "${DATA_ROOT}" /avengine /home/engine/.ssh
chown engine:engine /run/avengine

# Harden SSH for key-based login only.
sed -i 's/^#*PasswordAuthentication .*/PasswordAuthentication no/' /etc/ssh/sshd_config
sed -i 's/^#*PermitRootLogin .*/PermitRootLogin no/' /etc/ssh/sshd_config
sed -i 's/^#*PubkeyAuthentication .*/PubkeyAuthentication yes/' /etc/ssh/sshd_config

if [[ "${AVENGINE_AUTO_SWARM:-1}" != "0" ]]; then
  su -s /bin/bash engine -c "${BIN_ROOT}/start-crawler-swarm.sh"
fi

if [[ "${AVENGINE_AUTO_AGGREGATE:-1}" != "0" ]]; then
  su -s /bin/bash engine -c "${BIN_ROOT}/start-aggregate-watch.sh"
fi

exec /usr/sbin/sshd -D -e
