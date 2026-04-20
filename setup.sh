#!/usr/bin/env bash
# setup.sh — Sets up the AVEngine search server directly on a GCP VM.
#
# No Docker required. Builds from source, installs a systemd service
# (so it restarts automatically on failure and on reboot), and optionally
# configures nginx + TLS for the coordinator.
#
# Usage:
#   ./setup.sh
#
# Run as a regular user with sudo access (e.g. the default 'ubuntu' user).
# See deploysetup.md for a full walkthrough.

set -euo pipefail

# ── terminal colors ───────────────────────────────────────────────────────────

if [[ -t 1 ]]; then
  GREEN='\033[0;32m'
  YELLOW='\033[1;33m'
  CYAN='\033[0;36m'
  BOLD='\033[1m'
  RESET='\033[0m'
else
  GREEN='' YELLOW='' CYAN='' BOLD='' RESET=''
fi

info()  { echo -e "${CYAN}==>${RESET} $*"; }
warn()  { echo -e "${YELLOW}WARNING:${RESET} $*"; }
fatal() { echo -e "${YELLOW}ERROR:${RESET} $*" >&2; exit 1; }
ask()   { echo -e "${GREEN}?${RESET} $*"; }

# ── helpers ───────────────────────────────────────────────────────────────────

prompt() {
  local var="$1" question="$2" default="${3:-}"
  local display_default=""
  [[ -n "$default" ]] && display_default=" [${default}]"
  ask "${question}${display_default}: "
  local value
  read -r value </dev/tty
  [[ -z "$value" && -n "$default" ]] && value="$default"
  printf -v "$var" '%s' "$value"
}

prompt_yn() {
  local var="$1" question="$2" default="${3:-n}"
  local choices
  [[ "$default" == "y" ]] && choices="Y/n" || choices="y/N"
  ask "${question} (${choices}): "
  local value
  read -r value </dev/tty
  [[ -z "$value" ]] && value="$default"
  value="${value,,}"
  [[ "$value" == "y" || "$value" == "yes" ]] && printf -v "$var" 'y' || printf -v "$var" 'n'
}

validate_integer() {
  [[ "$1" =~ ^[0-9]+$ ]] || fatal "'$1' is not a valid integer."
}

validate_ip() {
  [[ -n "$1" ]] || fatal "IP address cannot be empty."
  if [[ "$1" =~ [[:space:]] ]]; then
    fatal "IP address cannot contain spaces."
  fi
}

# ── banner ────────────────────────────────────────────────────────────────────

echo ""
echo -e "${BOLD}╔══════════════════════════════════════════════════════╗${RESET}"
echo -e "${BOLD}║       AVEngine Search Cluster Setup                  ║${RESET}"
echo -e "${BOLD}╚══════════════════════════════════════════════════════╝${RESET}"
echo ""
echo "Builds the search server from source and installs it as a systemd"
echo "service that auto-restarts on failure and survives reboots."
echo ""
echo "This setup is pinned to the current three-VM cluster:"
echo "  node 0 / coordinator: new0  internal 10.128.0.3  external 34.55.231.113"
echo "  node 1 / worker:      new1  internal 10.128.0.4  external 104.197.43.60"
echo "  node 2 / worker:      new2  internal 10.128.0.7  external 35.226.21.73"
echo ""

# ── paths ─────────────────────────────────────────────────────────────────────

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SERVICE_NAME="avengine-search"
SERVICE_FILE="/etc/systemd/system/${SERVICE_NAME}.service"
PREPROCESS_SERVICE_NAME="avengine-tiered-preprocess"
PREPROCESS_SERVICE_FILE="/etc/systemd/system/${PREPROCESS_SERVICE_NAME}.service"
PREPROCESS_RUNNER="/usr/local/bin/avengine-tiered-preprocess"
ENV_FILE="/etc/avengine/search.env"
NGINX_CONF_SRC="${REPO_DIR}/deploy/nginx.conf"
NGINX_CONF_DST="/etc/nginx/conf.d/avengine.conf"
CERT_PATH="/etc/letsencrypt/live/theavengine.com/fullchain.pem"

[[ -f "${REPO_DIR}/CMakeLists.txt" ]] \
  || fatal "Cannot find CMakeLists.txt. Run this script from the repo root."

# ── step 1: role ──────────────────────────────────────────────────────────────

echo -e "${BOLD}Step 1: Node Role${RESET}"
echo "  coordinator  — new0 / node 0; runs nginx + search server; fans queries to workers"
echo "  worker       — new1 or new2; serves a local tiered index shard set"
echo "  single       — standalone, no peers; good for local testing"
echo ""

HOST_SHORT="$(hostname -s 2>/dev/null || hostname 2>/dev/null || true)"
DEFAULT_ROLE="single"
DEFAULT_NODE_ID=0
case "$HOST_SHORT" in
  new0)
    DEFAULT_ROLE="coordinator"
    DEFAULT_NODE_ID=0
    ;;
  new1)
    DEFAULT_ROLE="worker"
    DEFAULT_NODE_ID=1
    ;;
  new2)
    DEFAULT_ROLE="worker"
    DEFAULT_NODE_ID=2
    ;;
esac

prompt ROLE "Node role (coordinator / worker / single)" "$DEFAULT_ROLE"

case "$ROLE" in
  coordinator|worker|single) ;;
  *) fatal "Unknown role '$ROLE'. Choose: coordinator, worker, or single." ;;
esac

echo ""

# ── step 2: cluster config ────────────────────────────────────────────────────

NODE_ID=0
NUM_PEERS=1
declare -a PEER_IPS=()

if [[ "$ROLE" != "single" ]]; then
  echo -e "${BOLD}Step 2: Cluster Configuration${RESET}"

  # ── hardcoded cluster (3 nodes) ───────────────────────────────────────────
  # Peer traffic stays on the GCP VPC internal addresses.
  # External addresses:
  #   new0: 34.55.231.113
  #   new1: 104.197.43.60
  #   new2: 35.226.21.73
  NUM_PEERS=3
  PEER_IPS=(
    "10.128.0.3"  # node 0 — new0 coordinator
    "10.128.0.4"  # node 1 — new1 worker
    "10.128.0.7"  # node 2 — new2 worker
  )

  if [[ "$ROLE" == "coordinator" ]]; then
    NODE_ID=0
    info "Coordinator is always node 0."
  else
    echo "Cluster peers (hardcoded):"
    for i in "${!PEER_IPS[@]}"; do
      echo "    node ${i} → ${PEER_IPS[$i]}"
    done
    echo ""
    prompt NODE_ID_STR "This node's ID (1–2)" "$DEFAULT_NODE_ID"
    validate_integer "$NODE_ID_STR"
    NODE_ID="$NODE_ID_STR"
    [[ "$NODE_ID" -eq 0 ]]           && fatal "NODE_ID 0 is reserved for the coordinator."
    [[ "$NODE_ID" -ge "$NUM_PEERS" ]] && fatal "NODE_ID ($NODE_ID) must be less than NUM_PEERS ($NUM_PEERS)."
  fi
  echo ""
else
  info "Single-node mode — no peer configuration needed."
  echo ""
fi

# ── step 3: index directories ─────────────────────────────────────────────────

echo -e "${BOLD}Step 3: Index Shards${RESET}"
DEFAULT_OLD_INDEX="/mnt/data/oldcrawl"
DEFAULT_TIERED_INDEX="/mnt/data/tiered-index"
echo "Where are your existing post-crawl .sfi shards?"
echo "The setup will preprocess that directory into a tiered serving directory."
echo "Old shard names like A_00000.sfi, B_00000.sfi, A_anchor_00000.sfi,"
echo "B_anchor_00000.sfi are supported. Non-.sfi files such as index_group_000.txt"
echo "are ignored."
echo ""
prompt OLD_INDEX_DIR "Path to old index shard directory" "$DEFAULT_OLD_INDEX"
prompt INDEX_DIR "Path for tiered serving index directory" "$DEFAULT_TIERED_INDEX"
PREPROCESS_THREADS_DEFAULT="1"
prompt PREPROCESS_THREADS "Tiered preprocessor worker threads" "$PREPROCESS_THREADS_DEFAULT"
validate_integer "$PREPROCESS_THREADS"
[[ "$PREPROCESS_THREADS" -eq 0 ]] && fatal "Tiered preprocessor worker threads must be greater than zero."
prompt_yn PREPROCESS_BACKGROUND "Run tiered preprocessing in the background after service start?" "y"

# Warn if either path looks like a mount point rather than an index directory.
if [[ "$OLD_INDEX_DIR" == "/mnt" || "$INDEX_DIR" == "/mnt" ]]; then
  warn "That looks like a mount point, not an index directory."
  warn "Did you mean ${DEFAULT_OLD_INDEX} for old shards and ${DEFAULT_TIERED_INDEX} for serving?"
  prompt_yn FIX_INDEX_DIR "Use those defaults instead?" "y"
  if [[ "$FIX_INDEX_DIR" == "y" ]]; then
    OLD_INDEX_DIR="$DEFAULT_OLD_INDEX"
    INDEX_DIR="$DEFAULT_TIERED_INDEX"
  fi
fi

if [[ "$OLD_INDEX_DIR" == "$INDEX_DIR" ]]; then
  fatal "Old index directory and tiered serving directory must be different."
fi
echo ""

# ── step 4: nginx + SSL (coordinator only) ────────────────────────────────────

echo -e "${BOLD}Step 4: Shard Load Cutoff${RESET}"
echo "Optional: stop shard loading after a fixed number of seconds and serve"
echo "whatever shards finished loading. Leave blank for no cutoff."
echo ""
LOAD_TIMEOUT_DEFAULT=""
if [[ "$ROLE" != "single" ]]; then
  LOAD_TIMEOUT_DEFAULT="30"
fi
prompt SHARD_LOAD_TIMEOUT_SEC "Shard load cutoff in seconds" "$LOAD_TIMEOUT_DEFAULT"
if [[ -n "$SHARD_LOAD_TIMEOUT_SEC" ]]; then
  validate_integer "$SHARD_LOAD_TIMEOUT_SEC"
fi
echo ""

USE_NGINX="n"
if [[ "$ROLE" == "coordinator" ]]; then
  echo -e "${BOLD}Step 5: Nginx + SSL (coordinator only)${RESET}"
  echo "nginx terminates HTTPS for theavengine.com and proxies to the search server."
  echo ""
  echo "Before saying yes:"
  echo "  1. Assign a static external IP to this VM in GCP console"
  echo "  2. Set the theavengine.com DNS A record to that IP"
  echo "  3. Open TCP 80 and 443 inbound in your GCP firewall"
  echo ""
  prompt_yn USE_NGINX "Set up nginx + SSL?" "y"
  echo ""
fi

# ── host kernel tuning ────────────────────────────────────────────────────────

SYSCTL_CONF="/etc/sysctl.d/99-avengine.conf"
if [[ -f "$SYSCTL_CONF" ]]; then
  info "Host sysctl settings already applied — skipping."
else
  info "Applying host kernel tuning for m4-highmem-16..."
  sudo tee "$SYSCTL_CONF" > /dev/null <<'EOF'
# AVEngine search cluster — m4-highmem-16 / m4-hypermem-16 tuning

# The search process loads index bytes into memory. Keep default-ish reclaim
# behavior for non-index file cache and general host stability.
vm.swappiness = 60

# More file handles — needed for large numbers of .sfi index shards.
fs.file-max = 1048576

# Larger TCP listen backlog for the HTTP search server.
net.core.somaxconn = 65535

# Reuse TIME_WAIT sockets quickly (coordinator opens many short-lived
# connections to worker nodes).
net.ipv4.tcp_tw_reuse = 1
EOF
  sudo sysctl --system -q
  info "Kernel tuning applied."
fi
echo ""

# ── 300 GB data disk setup ────────────────────────────────────────────────────

DATA_MOUNT="/mnt/data"
if mountpoint -q "$DATA_MOUNT" 2>/dev/null; then
  info "Data disk already mounted at $DATA_MOUNT — skipping."
else
  # Find unmounted block devices >= 400 GB (the 300 GB data disk)
  DETECTED_DISK=""
  while IFS= read -r line; do
    dev=$(awk '{print $1}' <<< "$line")
    size=$(awk '{print $2}' <<< "$line")
    if [[ "$size" -ge 250000000000 ]]; then
      DETECTED_DISK="/dev/${dev}"
      break
    fi
  done < <(lsblk -b -o NAME,SIZE,TYPE,MOUNTPOINT --noheadings 2>/dev/null \
             | awk '$3=="disk" && $4==""')

  if [[ -n "$DETECTED_DISK" ]]; then
    echo -e "${BOLD}Data Disk Setup${RESET}"
    echo "Detected unmounted large disk: ${DETECTED_DISK}"
    echo "This should be the 300 GB data disk where index shards will be stored."
    echo ""
    prompt_yn FORMAT_DISK "Format and mount ${DETECTED_DISK} at ${DATA_MOUNT}?" "y"
    echo ""

    if [[ "$FORMAT_DISK" == "y" ]]; then
      if sudo blkid "$DETECTED_DISK" &>/dev/null; then
        warn "${DETECTED_DISK} already has a filesystem — mounting without formatting."
      else
        info "Formatting ${DETECTED_DISK} as ext4..."
        sudo mkfs.ext4 -F -L avengine-data "$DETECTED_DISK"
      fi

      info "Mounting at ${DATA_MOUNT} (SSD options: noatime, discard)..."
      sudo mkdir -p "$DATA_MOUNT"
      sudo mount -o noatime,discard "$DETECTED_DISK" "$DATA_MOUNT"
      sudo chown "$USER":"$USER" "$DATA_MOUNT"

      DISK_UUID="$(sudo blkid -s UUID -o value "$DETECTED_DISK")"
      if ! grep -qF "$DISK_UUID" /etc/fstab 2>/dev/null; then
        # noatime: skip access-time writes (reduces write amplification on SSD)
        # discard: enables TRIM so the SSD reclaims freed blocks
        echo "UUID=${DISK_UUID}  ${DATA_MOUNT}  ext4  noatime,discard,nofail  0  2" \
          | sudo tee -a /etc/fstab > /dev/null
        info "Added to /etc/fstab (auto-mounts on reboot)."
      fi
    fi
  else
    info "No unmounted large disk found — index will use local path."
  fi
fi
echo ""

# Validate old shard dir and create output dir if needed.
if [[ ! -d "$OLD_INDEX_DIR" ]]; then
  warn "Creating empty old index directory at ${OLD_INDEX_DIR}"
  warn "Copy your existing .sfi shards there before preprocessing will produce results."
  mkdir -p "$OLD_INDEX_DIR"
fi

if [[ ! -d "$INDEX_DIR" ]]; then
  info "Creating tiered serving index directory at ${INDEX_DIR}"
  mkdir -p "$INDEX_DIR"
fi

# ── install build dependencies ────────────────────────────────────────────────

info "Installing build dependencies..."
sudo apt-get update -qq
sudo apt-get install -y --no-install-recommends \
  build-essential \
  cmake \
  libssl-dev \
  zlib1g-dev \
  git \
  ca-certificates

if [[ "$USE_NGINX" == "y" ]]; then
  sudo apt-get install -y --no-install-recommends \
    nginx \
    certbot \
    python3-certbot-nginx
fi
echo ""

# ── build search_server + preprocessor ────────────────────────────────────────

info "Building search_server and tiered_shard_preprocessor (this takes a few minutes)..."
cmake -S "${REPO_DIR}" -B "${REPO_DIR}/build" \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=OFF \
  > /dev/null
cmake --build "${REPO_DIR}/build" --target search_server tiered_shard_preprocessor -j"$(nproc)"
info "Build complete:"
echo "    ${REPO_DIR}/build/search_server"
echo "    ${REPO_DIR}/build/tiered_shard_preprocessor"
echo ""

# ── inspect old shards ────────────────────────────────────────────────────────

echo -e "${BOLD}Preprocess Tiered Index${RESET}"
OLD_SFI_COUNT="$(find "$OLD_INDEX_DIR" -maxdepth 1 -type f -name '*.sfi' 2>/dev/null | wc -l | tr -d ' ')"
if [[ "$OLD_SFI_COUNT" == "0" ]]; then
  warn "No .sfi shards found in ${OLD_INDEX_DIR}."
  warn "The server will start, but ${INDEX_DIR} may be empty and return no results."
else
  info "Found ${OLD_SFI_COUNT} old shard(s) in ${OLD_INDEX_DIR}."
  if [[ "$PREPROCESS_BACKGROUND" == "y" ]]; then
    info "Tiered preprocessing will run in the background after the search service starts."
  else
    info "Tiered preprocessing will run in the foreground through systemd."
  fi
fi
echo ""

# ── write environment config ──────────────────────────────────────────────────

info "Writing ${ENV_FILE}..."
sudo mkdir -p "$(dirname "$ENV_FILE")"

{
  echo "# AVEngine search server environment — auto-generated by setup.sh"
  echo "# Edit as needed, then: sudo systemctl restart ${SERVICE_NAME}"
  echo ""
  echo "OLD_INDEX_DIR=${OLD_INDEX_DIR}"
  echo "INDEX_DIR=${INDEX_DIR}"
  echo "PREPROCESS_THREADS=${PREPROCESS_THREADS}"
  echo "PREPROCESS_TMPDIR=/mnt/data/tmp"
  echo ""

  if [[ "$ROLE" == "single" ]]; then
    echo "SEARCH_ROLE=coordinator"
    echo "NODE_ID=0"
    echo "NUM_PEERS=1"
  elif [[ "$ROLE" == "coordinator" ]]; then
    echo "SEARCH_ROLE=coordinator"
    echo "NODE_ID=0"
    echo "NUM_PEERS=${NUM_PEERS}"
  else
    echo "SEARCH_ROLE=worker"
    echo "NODE_ID=${NODE_ID}"
    echo "NUM_PEERS=${NUM_PEERS}"
  fi

  # Reserve 2 vCPUs for the OS; use the rest for parallel shard queries.
  # Computed from the actual hardware so this works on any machine type.
  SHARD_WORKERS=$(( $(nproc) - 2 ))
  [[ "$SHARD_WORKERS" -lt 1 ]] && SHARD_WORKERS=1
  echo ""
  echo "# Use all but 2 vCPUs for parallel shard queries (auto-detected: $(nproc) total)"
  echo "SEARCH_SHARD_WORKERS=${SHARD_WORKERS}"
  if [[ -n "$SHARD_LOAD_TIMEOUT_SEC" ]]; then
    echo ""
    echo "# Optional shard-load cutoff. When reached, the server serves the shards"
    echo "# that finished loading so far instead of waiting for the full corpus."
    echo "SEARCH_LOAD_TIMEOUT_SEC=${SHARD_LOAD_TIMEOUT_SEC}"
  fi

} | sudo tee "$ENV_FILE" > /dev/null

sudo chmod 640 "$ENV_FILE"

# ── write peer_list.txt ───────────────────────────────────────────────────────
# The search_server binary reads peer IPs from this file at startup (node.cpp).
# Format: one entry per line — "<node_id> <ip_address>"
# File must live in the working directory (REPO_DIR) set in the systemd service.

PEER_LIST="${REPO_DIR}/peer_list.txt"
if [[ "${#PEER_IPS[@]}" -gt 0 ]]; then
  info "Writing ${PEER_LIST}..."
  {
    echo "# AVEngine peer list — auto-generated by setup.sh"
    echo "# Format: <node_id> <ip_address>"
    for i in "${!PEER_IPS[@]}"; do
      echo "${i} ${PEER_IPS[$i]}"
    done
  } > "$PEER_LIST"
  info "Peers registered:"
  for i in "${!PEER_IPS[@]}"; do
    echo "    node ${i} → ${PEER_IPS[$i]}"
  done
fi
echo ""

# ── install tiered preprocessor service ───────────────────────────────────────

info "Installing tiered preprocessor runner: ${PREPROCESS_RUNNER}..."

sudo tee "$PREPROCESS_RUNNER" > /dev/null <<EOF
#!/usr/bin/env bash
set -euo pipefail

SERVICE_NAME="${SERVICE_NAME}"
REPO_DIR="${REPO_DIR}"
ENV_FILE="${ENV_FILE}"
RUN_AS_USER="${USER}"
RUN_AS_GROUP="${USER}"

if [[ -f "\$ENV_FILE" ]]; then
  set -a
  # shellcheck disable=SC1090
  . "\$ENV_FILE"
  set +a
fi

: "\${OLD_INDEX_DIR:?OLD_INDEX_DIR is not set}"
: "\${INDEX_DIR:?INDEX_DIR is not set}"
: "\${PREPROCESS_THREADS:=1}"
: "\${PREPROCESS_TMPDIR:=/mnt/data/tmp}"

LOCK_DIR="/tmp/avengine-tiered-preprocess.lock"
if ! mkdir "\$LOCK_DIR" 2>/dev/null; then
  echo "[tiered-preprocess] another preprocess job is already running"
  exit 0
fi
trap 'rmdir "\$LOCK_DIR" 2>/dev/null || true' EXIT

if [[ ! -d "\$OLD_INDEX_DIR" ]]; then
  echo "[tiered-preprocess] old index directory does not exist: \$OLD_INDEX_DIR" >&2
  exit 1
fi

OLD_SFI_COUNT="\$(find "\$OLD_INDEX_DIR" -maxdepth 1 -type f -name '*.sfi' 2>/dev/null | wc -l | tr -d ' ')"
if [[ "\$OLD_SFI_COUNT" == "0" ]]; then
  echo "[tiered-preprocess] no .sfi shards found in \$OLD_INDEX_DIR; nothing to do"
  exit 0
fi

STAGING="\${INDEX_DIR}.staging"
PREVIOUS="\${INDEX_DIR}.previous"
CHECKPOINT="\${STAGING}/.tiered_preprocess.checkpoint"
PARENT_DIR="\$(dirname "\$INDEX_DIR")"

echo "[tiered-preprocess] input=\$OLD_INDEX_DIR output=\$INDEX_DIR staging=\$STAGING shards=\$OLD_SFI_COUNT threads=\$PREPROCESS_THREADS checkpoint=\$CHECKPOINT"

mkdir -p "\$PARENT_DIR" "\$PREPROCESS_TMPDIR"
chown "\$RUN_AS_USER:\$RUN_AS_GROUP" "\$PARENT_DIR" "\$PREPROCESS_TMPDIR" 2>/dev/null || true
install -d -o "\$RUN_AS_USER" -g "\$RUN_AS_GROUP" "\$STAGING"

runuser -u "\$RUN_AS_USER" -- env TMPDIR="\$PREPROCESS_TMPDIR" \\
  "\$REPO_DIR/build/tiered_shard_preprocessor" \\
  --input "\$OLD_INDEX_DIR" \\
  --output "\$STAGING" \\
  --checkpoint "\$CHECKPOINT" \\
  --threads "\$PREPROCESS_THREADS" \\
  --force

TIERED_SFI_COUNT="\$(find "\$STAGING" -maxdepth 1 -type f -name '*.sfi' 2>/dev/null | wc -l | tr -d ' ')"
if [[ "\$TIERED_SFI_COUNT" == "0" ]]; then
  echo "[tiered-preprocess] preprocessor produced no .sfi shards; leaving existing index untouched" >&2
  exit 1
fi

rm -rf "\$PREVIOUS"
if [[ -d "\$INDEX_DIR" ]]; then
  mv "\$INDEX_DIR" "\$PREVIOUS"
fi
mv "\$STAGING" "\$INDEX_DIR"
chown -R "\$RUN_AS_USER:\$RUN_AS_GROUP" "\$INDEX_DIR" 2>/dev/null || true

echo "[tiered-preprocess] swapped \$TIERED_SFI_COUNT tiered shard(s) into \$INDEX_DIR"
systemctl try-restart "\$SERVICE_NAME"
rm -rf "\$PREVIOUS"
echo "[tiered-preprocess] done"
EOF

sudo chmod 755 "$PREPROCESS_RUNNER"

info "Installing systemd service: ${PREPROCESS_SERVICE_NAME}..."

sudo tee "$PREPROCESS_SERVICE_FILE" > /dev/null <<EOF
[Unit]
Description=AVEngine Tiered Shard Preprocessor
Documentation=file://${REPO_DIR}/deploysetup.md
After=${SERVICE_NAME}.service
Wants=${SERVICE_NAME}.service

[Service]
Type=oneshot
EnvironmentFile=${ENV_FILE}
ExecStart=${PREPROCESS_RUNNER}
TimeoutStartSec=infinity
Nice=5
IOSchedulingClass=best-effort
IOSchedulingPriority=6
StandardOutput=journal
StandardError=journal
SyslogIdentifier=avengine-tiered-preprocess

[Install]
WantedBy=multi-user.target
EOF

echo ""

# ── install systemd service ───────────────────────────────────────────────────

info "Installing systemd service: ${SERVICE_NAME}..."

sudo tee "$SERVICE_FILE" > /dev/null <<EOF
[Unit]
Description=AVEngine Search Server
Documentation=file://${REPO_DIR}/deploysetup.md
After=network-online.target
Wants=network-online.target
# Give up after 5 failures in 2 minutes to avoid a runaway restart loop.
StartLimitIntervalSec=120
StartLimitBurst=5

[Service]
Type=simple
User=${USER}
Group=${USER}

# Working directory must contain ranker_weights.conf
WorkingDirectory=${REPO_DIR}

EnvironmentFile=${ENV_FILE}
ExecStart=${REPO_DIR}/build/search_server 8080 ${INDEX_DIR}

# ── restartability ────────────────────────────────────────────────────────────
# Restart automatically on crash or signal. Wait 5 s between attempts.
Restart=on-failure
RestartSec=5s

# ── resource limits ───────────────────────────────────────────────────────────
# Large file descriptor limit for hundreds of .sfi index shards.
LimitNOFILE=1048576

# ── memory cap ────────────────────────────────────────────────────────────────
# The search server loads tiered index bytes into owned process memory.
# These limits bound the server if the loaded index is larger than expected.
#
# MemoryHigh: soft limit — systemd starts applying memory pressure here.
# MemoryMax:  hard limit — kernel will OOM-kill only this process (not the
#             whole machine) if the process exceeds the cap. Systemd restarts it.
#
# Adjust these two values based on how much RAM your VM actually has.
# Leave ~1 GB headroom for the OS. Example for an 8 GB VM: High=6G, Max=7G.
MemoryHigh=70%
MemoryMax=85%
# Do not use swap — re-reading from the SSD index is faster than swap I/O.
MemorySwapMax=0

# ── logging ───────────────────────────────────────────────────────────────────
StandardOutput=journal
StandardError=journal
SyslogIdentifier=avengine-search

[Install]
WantedBy=multi-user.target
EOF

sudo systemctl daemon-reload
sudo systemctl enable "${SERVICE_NAME}"
sudo systemctl restart "${SERVICE_NAME}"
info "Search service started and enabled (auto-starts on reboot)."

if [[ "$OLD_SFI_COUNT" != "0" ]]; then
  if [[ "$PREPROCESS_BACKGROUND" == "y" ]]; then
    sudo systemctl start --no-block "${PREPROCESS_SERVICE_NAME}"
    info "Tiered preprocessing started in the background."
  else
    sudo systemctl start "${PREPROCESS_SERVICE_NAME}"
    info "Tiered preprocessing completed."
  fi
else
  warn "Skipping tiered preprocessing because no old shards were found."
fi
echo ""

# ── nginx + TLS (coordinator only) ────────────────────────────────────────────

if [[ "$USE_NGINX" == "y" ]]; then

  # -- TLS certificate ---------------------------------------------------------
  if [[ -f "$CERT_PATH" ]]; then
    info "TLS certificate already exists — skipping certbot."
  else
    echo -e "${BOLD}Getting a TLS certificate via certbot${RESET}"
    echo "certbot will use the nginx plugin (no need to stop nginx first)."
    echo ""
    prompt_yn RUN_CERTBOT "Run certbot now?" "y"
    if [[ "$RUN_CERTBOT" == "y" ]]; then
      info "Running certbot — follow the prompts (enter your email when asked)..."
      sudo certbot --nginx -d theavengine.com -d www.theavengine.com
      echo ""
    else
      warn "Skipping certbot. nginx will fail to serve HTTPS without a cert."
      warn "Run manually:  sudo certbot --nginx -d theavengine.com -d www.theavengine.com"
      echo ""
    fi
  fi

  # Auto-renewal is handled by the certbot systemd timer installed with the
  # package (check with: systemctl status certbot.timer)

  # -- nginx config ------------------------------------------------------------
  info "Installing nginx config..."
  sudo cp "$NGINX_CONF_SRC" "$NGINX_CONF_DST"
  sudo nginx -t && sudo systemctl reload nginx
  sudo systemctl enable nginx
  info "nginx configured and running."
  echo ""
fi

# ── summary ───────────────────────────────────────────────────────────────────

echo -e "${BOLD}╔══════════════════════════════════════════════════════╗${RESET}"
echo -e "${BOLD}║                    Setup Complete                    ║${RESET}"
echo -e "${BOLD}╚══════════════════════════════════════════════════════╝${RESET}"
echo ""
echo -e "  Role:         ${BOLD}${ROLE}${RESET} (NODE_ID=${NODE_ID}, NUM_PEERS=${NUM_PEERS})"
echo -e "  Old shards:   ${OLD_INDEX_DIR}"
echo -e "  Tiered index: ${INDEX_DIR}"
echo -e "  Preprocess:   ${PREPROCESS_THREADS} thread(s), background=${PREPROCESS_BACKGROUND}"
echo -e "  Config:       ${ENV_FILE}"
echo -e "  Binary:       ${REPO_DIR}/build/search_server"
if [[ -n "$SHARD_LOAD_TIMEOUT_SEC" ]]; then
  echo -e "  Load cutoff:  ${SHARD_LOAD_TIMEOUT_SEC}s"
fi
echo ""
echo "  Service commands:"
echo "    sudo systemctl status ${SERVICE_NAME}    # check status"
echo "    sudo systemctl restart ${SERVICE_NAME}   # restart manually"
echo "    sudo journalctl -fu ${SERVICE_NAME}      # stream logs"
echo "    sudo systemctl status ${PREPROCESS_SERVICE_NAME}    # check retiering"
echo "    sudo journalctl -fu ${PREPROCESS_SERVICE_NAME}      # stream retiering logs"
echo ""
echo "  Test the search API:"
if [[ "$USE_NGINX" == "y" ]]; then
  echo "    curl \"https://theavengine.com/api/search?q=test\""
else
  echo "    curl \"http://localhost:8080/api/search?q=test\""
fi
echo ""

if [[ "${#PEER_IPS[@]}" -gt 0 ]]; then
  echo "  Make sure GCP firewall allows TCP 47857 inbound from all peer IPs:"
  for i in "${!PEER_IPS[@]}"; do
    echo "    Node ${i}: ${PEER_IPS[$i]}"
  done
  echo "  See deploysetup.md for the gcloud firewall command."
  echo ""
fi

TIERED_SFI_COUNT="$(find "$INDEX_DIR" -maxdepth 1 -type f -name '*.sfi' 2>/dev/null | wc -l | tr -d ' ')"
if [[ "$TIERED_SFI_COUNT" == "0" ]]; then
  warn "Tiered serving index has no .sfi shards — the server will start but return no results."
  warn "Copy your old .sfi shards to ${OLD_INDEX_DIR}, rerun ./setup.sh, then restart:"
  warn "  sudo systemctl restart ${SERVICE_NAME}"
  echo ""
fi

echo "  See deploysetup.md for full documentation."
echo ""
