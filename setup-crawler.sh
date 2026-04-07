#!/usr/bin/env bash

set -e

cat <<EOF > /etc/systemd/system/crawler.service
[Unit]
Description=AVEngine crawler node
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
WorkingDirectory=/mnt/crawler-data/
Environment=NODE_ID=$1
Environment=NUM_PEERS=$2
Environment=CRAWLER_WORKERS=1024
ExecStart=/home/bitmoder/crawler_main
Restart=always
RestartSec=2
LimitNOFILE=131072

[Install]
WantedBy=multi-user.target
EOF


# --- Persistent limits ---
cat <<EOF >> /etc/security/limits.conf
*    soft nofile 131072
*    hard nofile 131072
*    soft nproc  65536
*    hard nproc  65536
*    soft stack  1024
*    hard stack  1024
EOF

# --- Kernel tuning ---
cat <<EOF > /etc/sysctl.d/99-crawler.conf
fs.file-max = 2097152
kernel.pid_max = 4194304
kernel.threads-max = 1000000
vm.max_map_count = 1048576

net.ipv4.ip_local_port_range = 1024 65535
net.ipv4.tcp_tw_reuse = 1
net.core.somaxconn = 65535
net.core.netdev_max_backlog = 65536
net.ipv4.tcp_fin_timeout = 15
net.ipv4.tcp_max_syn_backlog = 65536

net.core.rmem_max = 16777216
net.core.wmem_max = 16777216
EOF

sysctl -p /etc/sysctl.d/99-crawler.conf

# --- Unbound ---
apt-get install -y unbound

cat <<EOF > /etc/unbound/unbound.conf
server:
    # Listen only locally
    interface: 127.0.0.1
    port: 53
    do-ip4: yes
    do-ip6: no
    do-udp: yes
    do-tcp: yes

    # Access control
    access-control: 127.0.0.0/8 allow

    # High-concurrency settings for crawler workload
    num-threads: 4
    msg-cache-slabs: 8
    rrset-cache-slabs: 8
    infra-cache-slabs: 8
    key-cache-slabs: 8

    # Large cache to absorb crawler DNS bursts
    msg-cache-size: 256m
    rrset-cache-size: 512m

    # Allow many concurrent outstanding queries
    outgoing-range: 8192
    num-queries-per-thread: 4096
    jostle-timeout: 200

    # Prefetch popular records before TTL expires
    prefetch: yes
    prefetch-key: yes

    # Slightly ignore TTLs to serve from cache more aggressively
    cache-min-ttl: 60
    cache-max-ttl: 86400

    # Logging (disable in prod for performance)
    verbosity: 1
    logfile: /var/log/unbound.log

# Upstream resolvers (forward queries to Google + Cloudflare)
forward-zone:
    name: "."
    forward-addr: 8.8.8.8
    forward-addr: 8.8.4.4
    forward-addr: 1.1.1.1
    forward-addr: 1.0.0.1
EOF

# Disable systemd-resolved if running (conflicts on port 53)
systemctl disable --now systemd-resolved 2>/dev/null || true
rm -f /etc/resolv.conf

# Point the system at unbound
echo "nameserver 127.0.0.1" > /etc/resolv.conf

# Lock resolv.conf so NetworkManager/DHCP can't overwrite it
chattr +i /etc/resolv.conf

systemctl enable unbound
systemctl restart unbound

DATA_DEV=/dev/disk/by-id/google-crawler-disk
MOUNT_DIR=/mnt/crawler-data

if [[ ! -b "$DATA_DEV" ]]; then
  echo "missing attached data disk at $DATA_DEV" >&2
  exit 1
fi

if ! blkid "$DATA_DEV" >/dev/null 2>&1; then
  mkfs.ext4 -F "$DATA_DEV"
fi

mkdir -p "$MOUNT_DIR"
UUID=$(blkid -s UUID -o value "$DATA_DEV")
if ! grep -q "UUID=${UUID}" /etc/fstab; then
  echo "UUID=${UUID} ${MOUNT_DIR} ext4 defaults,nofail 0 2" >>/etc/fstab
fi

if ! mountpoint -q "$MOUNT_DIR"; then
  mount "$MOUNT_DIR"
fi

rm /mnt/crawler-data/* -r
cp seed_list.txt banned_urls.txt banned_words.txt peer_list.txt /mnt/crawler-data/

systemctl daemon-reload
systemctl enable crawler.service
