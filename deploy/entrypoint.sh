#!/usr/bin/env bash
set -euo pipefail

DATA_ROOT="${AVENGINE_DATA_ROOT:-/var/lib/avengine}"

if [[ -n "${AVENGINE_SSH_PUBKEY:-}" ]]; then
  echo "${AVENGINE_SSH_PUBKEY}" > /home/engine/.ssh/authorized_keys
  chown engine:engine /home/engine/.ssh/authorized_keys
  chmod 600 /home/engine/.ssh/authorized_keys
fi

mkdir -p "${DATA_ROOT}"
chown -R engine:engine "${DATA_ROOT}" /opt/avengine /home/engine/.ssh

# Harden SSH for key-based login only.
sed -i 's/^#*PasswordAuthentication .*/PasswordAuthentication no/' /etc/ssh/sshd_config
sed -i 's/^#*PermitRootLogin .*/PermitRootLogin no/' /etc/ssh/sshd_config
sed -i 's/^#*PubkeyAuthentication .*/PubkeyAuthentication yes/' /etc/ssh/sshd_config

if [[ "${AVENGINE_AUTO_SWARM:-1}" != "0" ]]; then
  su -s /bin/bash engine -c "/opt/avengine/bin/start-crawler-swarm.sh"
fi

if [[ "${AVENGINE_AUTO_AGGREGATE:-1}" != "0" ]]; then
  su -s /bin/bash engine -c \
    "/opt/avengine/bin/aggregate-corpus-index.sh --watch --interval ${AVENGINE_AGGREGATE_INTERVAL_SECONDS:-300}" \
    >> "${DATA_ROOT}/aggregate.log" 2>&1 &
fi

exec /usr/sbin/sshd -D -e
