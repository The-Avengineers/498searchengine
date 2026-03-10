#!/usr/bin/env bash
set -euo pipefail

if [[ -n "${AVENGINE_SSH_PUBKEY:-}" ]]; then
  echo "${AVENGINE_SSH_PUBKEY}" > /home/engine/.ssh/authorized_keys
  chown engine:engine /home/engine/.ssh/authorized_keys
  chmod 600 /home/engine/.ssh/authorized_keys
fi

# Harden SSH for key-based login only.
sed -i 's/^#*PasswordAuthentication .*/PasswordAuthentication no/' /etc/ssh/sshd_config
sed -i 's/^#*PermitRootLogin .*/PermitRootLogin no/' /etc/ssh/sshd_config
sed -i 's/^#*PubkeyAuthentication .*/PubkeyAuthentication yes/' /etc/ssh/sshd_config

exec /usr/sbin/sshd -D -e
