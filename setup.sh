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

# ── paths ─────────────────────────────────────────────────────────────────────

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SERVICE_NAME="avengine-search"
SERVICE_FILE="/etc/systemd/system/${SERVICE_NAME}.service"
ENV_FILE="/etc/avengine/search.env"
NGINX_CONF_SRC="${REPO_DIR}/deploy/nginx.conf"
NGINX_CONF_DST="/etc/nginx/conf.d/avengine.conf"
CERT_PATH="/etc/letsencrypt/live/theavengine.com/fullchain.pem"
DATA_ROOT="/mnt/data"
RAW_INDEX_SHARD_DIR="${DATA_ROOT}/crawlers/0"
SERVING_SHARD_DIR="${DATA_ROOT}/searchshards"

[[ -f "${REPO_DIR}/CMakeLists.txt" ]] \
  || fatal "Cannot find CMakeLists.txt. Run this script from the repo root."

# ── step 1: role ──────────────────────────────────────────────────────────────

echo -e "${BOLD}Step 1: Node Role${RESET}"
echo "  coordinator  — node 0; runs nginx + search server; fans queries to workers"
echo "  worker       — node 1, 2, ...; serves a local index shard"
echo "  single       — standalone, no peers; good for local testing"
echo ""
prompt ROLE "Node role (coordinator / worker / single)" "single"

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
  # GCP internal VPC IPs:
  #   new0 = node 0, coordinator/main machine
  #   new1 = node 1, worker
  #   new2 = node 2, worker
  NUM_PEERS=3
  PEER_IPS=(
    "10.128.0.3"  # node 0 — new0 (coordinator/main)
    "10.128.0.4"  # node 1 — new1
    "10.128.0.7"  # node 2 — new2
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
    prompt NODE_ID_STR "This node's ID (1–2)" ""
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

# ── step 3: index directory ───────────────────────────────────────────────────

echo -e "${BOLD}Step 3: Index Shards${RESET}"
DEFAULT_RAW_INDEX="${RAW_INDEX_SHARD_DIR}"
DEFAULT_INDEX="${SERVING_SHARD_DIR}"
echo "The search server loads rebuilt .ssi serving shards."
echo "Existing .sfi and anchor_*.sfi shards are read from an input directory,"
echo "then rebuilt into a separate output directory."
echo ""
prompt RAW_INDEX_DIR "Path to existing .sfi shard directory" "$DEFAULT_RAW_INDEX"
prompt INDEX_DIR "Output directory for rebuilt .ssi serving shards" "$DEFAULT_INDEX"

# Warn if the path looks like a mount point rather than an index directory.
if [[ "$RAW_INDEX_DIR" == "/mnt" || "$INDEX_DIR" == "/mnt" ]]; then
  warn "That looks like a mount point, not an index directory."
  warn "Did you mean raw=${DEFAULT_RAW_INDEX} and serving=${DEFAULT_INDEX}?"
  prompt_yn FIX_INDEX_DIR "Use those defaults instead?" "y"
  if [[ "$FIX_INDEX_DIR" == "y" ]]; then
    RAW_INDEX_DIR="$DEFAULT_RAW_INDEX"
    INDEX_DIR="$DEFAULT_INDEX"
  fi
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

# Allow the kernel to reclaim file-backed index pages when RAM is low.
# The index is mmap'd lazily so pages fault in on demand and can be
# evicted and re-read from SSD as needed. Default (60) is fine here.
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

# Create shard dirs if they don't exist
mkdir -p "$RAW_INDEX_DIR" "$INDEX_DIR"
if [[ ! -d "$INDEX_DIR" ]]; then
  warn "Creating empty index directory at ${INDEX_DIR}"
  warn "Run the serving rebuild before the search server will return results."
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

# ── build search_server ───────────────────────────────────────────────────────

info "Building search_server and serving-index rebuild tool (this takes a few minutes)..."
cmake -S "${REPO_DIR}" -B "${REPO_DIR}/build" \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=OFF \
  > /dev/null
cmake --build "${REPO_DIR}/build" --target search_server rebuild_serving_index -j"$(nproc)"
info "Build complete: ${REPO_DIR}/build/search_server"
info "Build complete: ${REPO_DIR}/build/rebuild_serving_index"
echo ""

# ── rebuild serving shards ────────────────────────────────────────────────────

echo -e "${BOLD}Serving Shard Rebuild${RESET}"
echo "Input .sfi shards:  ${RAW_INDEX_DIR}"
echo "Output .ssi shards: ${INDEX_DIR}"
echo ""
prompt_yn RUN_SERVING_REBUILD "Rebuild serving shards now?" "y"
if [[ "$RUN_SERVING_REBUILD" == "y" ]]; then
  if find "$RAW_INDEX_DIR" -maxdepth 1 -type f -name '*.sfi' -print -quit | grep -q .; then
    info "Rebuilding serving shards into ${INDEX_DIR}..."
    rm -f "${INDEX_DIR}"/serving_*.ssi
    "${REPO_DIR}/build/rebuild_serving_index" \
      --input-dir "$RAW_INDEX_DIR" \
      --output-dir "$INDEX_DIR"
    info "Serving shard rebuild complete."
  else
    warn "No .sfi shards found in ${RAW_INDEX_DIR}; skipping serving rebuild."
    warn "After shards exist, run:"
    warn "  ${REPO_DIR}/build/rebuild_serving_index --input-dir ${RAW_INDEX_DIR} --output-dir ${INDEX_DIR}"
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
# The index is mmap'd from SSD — these limits tell the kernel to evict index
# pages back to disk when RAM gets tight, rather than OOM-killing everything.
#
# MemoryHigh: soft limit — kernel starts reclaiming pages aggressively here.
# MemoryMax:  hard limit — kernel will OOM-kill only this process (not the
#             whole machine) if reclaim isn't enough. Systemd restarts it.
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
info "Service started and enabled (auto-starts on reboot)."
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
echo -e "  Input shards: ${RAW_INDEX_DIR}"
echo -e "  Search shards: ${INDEX_DIR}"
echo -e "  Config:       ${ENV_FILE}"
echo -e "  Binary:       ${REPO_DIR}/build/search_server"
echo -e "  Rebuilder:    ${REPO_DIR}/build/rebuild_serving_index"
if [[ -n "$SHARD_LOAD_TIMEOUT_SEC" ]]; then
  echo -e "  Load cutoff:  ${SHARD_LOAD_TIMEOUT_SEC}s"
fi
echo ""
echo "  Service commands:"
echo "    sudo systemctl status ${SERVICE_NAME}    # check status"
echo "    sudo systemctl restart ${SERVICE_NAME}   # restart manually"
echo "    sudo journalctl -fu ${SERVICE_NAME}      # stream logs"
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

if [[ -z "$(ls -A "$INDEX_DIR" 2>/dev/null)" ]]; then
  warn "Serving shard directory is empty — the server will start but return no results."
  warn "Run the serving rebuild manually:"
  warn "  ${REPO_DIR}/build/rebuild_serving_index --input-dir ${RAW_INDEX_DIR} --output-dir ${INDEX_DIR}"
  warn "  sudo systemctl restart ${SERVICE_NAME}"
  echo ""
fi

echo "  See deploysetup.md for full documentation."
echo ""
