#!/usr/bin/env bash
# setup.sh — Interactive setup for the AVEngine distributed search cluster.
#
# Run this on any GCP VM (coordinator or worker) to configure and start the
# search server. No arguments needed — the script asks you everything.
#
# Usage:
#   ./setup.sh
#
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
  # prompt <var_name> <question> [default]
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
  # prompt_yn <var_name> <question> <default_y_or_n>
  local var="$1" question="$2" default="${3:-n}"
  local choices="y/n"
  [[ "$default" == "y" ]] && choices="Y/n" || choices="y/N"
  ask "${question} (${choices}): "
  local value
  read -r value </dev/tty
  [[ -z "$value" ]] && value="$default"
  value="${value,,}"  # lowercase
  [[ "$value" == "y" || "$value" == "yes" ]] && printf -v "$var" 'y' || printf -v "$var" 'n'
}

validate_integer() {
  [[ "$1" =~ ^[0-9]+$ ]] || fatal "'$1' is not a valid integer."
}

validate_ip() {
  local ip="$1"
  # Accept bare hostnames too (GCP internal hostnames), just check it's non-empty and not obviously wrong
  [[ -n "$ip" ]] || fatal "IP address cannot be empty."
  [[ "$ip" =~ [[:space:]] ]] && fatal "IP address cannot contain spaces."
}

# ── banner ────────────────────────────────────────────────────────────────────

echo ""
echo -e "${BOLD}╔══════════════════════════════════════════════════════╗${RESET}"
echo -e "${BOLD}║       AVEngine Search Cluster Setup                  ║${RESET}"
echo -e "${BOLD}╚══════════════════════════════════════════════════════╝${RESET}"
echo ""
echo "This script sets up the AVEngine search server on this machine."
echo "It will ask a few questions, write a config file, and start the"
echo "container with Docker Compose."
echo ""

# ── repo location ─────────────────────────────────────────────────────────────

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$SCRIPT_DIR"
COMPOSE_SEARCH="${REPO_DIR}/infra/deploy/docker-compose.search.yml"
COMPOSE_NGINX="${REPO_DIR}/deploy/docker-compose.nginx.yml"
ENV_FILE="${REPO_DIR}/.env.search"

[[ -f "$COMPOSE_SEARCH" ]] || fatal "Cannot find $COMPOSE_SEARCH. Run this script from the repo root."

# ── step 1: role ──────────────────────────────────────────────────────────────

echo -e "${BOLD}Step 1: Node Role${RESET}"
echo "  coordinator  — node 0; runs nginx + search server; queries fan out to workers"
echo "  worker       — node 1, 2, ...; serves a local index shard; answers coordinator"
echo "  single       — standalone; no distributed peers; good for local testing"
echo ""
prompt ROLE "Node role (coordinator / worker / single)" "single"

case "$ROLE" in
  coordinator|worker|single) ;;
  *) fatal "Unknown role '$ROLE'. Choose: coordinator, worker, or single." ;;
esac

echo ""

# ── step 2: cluster size + peer IPs ──────────────────────────────────────────

NODE_ID=0
NUM_PEERS=1
declare -a PEER_IPS=()

if [[ "$ROLE" == "single" ]]; then
  info "Single-node mode — no peer configuration needed."
  NUM_PEERS=1
  NODE_ID=0
else
  echo -e "${BOLD}Step 2: Cluster Configuration${RESET}"
  echo "You need the internal (VPC) IP addresses of every node, including this one."
  echo "On GCP: VM console → Network interfaces → Primary internal IP."
  echo ""

  prompt NUM_PEERS_STR "Total number of nodes in the cluster (coordinator + all workers)" "2"
  validate_integer "$NUM_PEERS_STR"
  NUM_PEERS="$NUM_PEERS_STR"

  if [[ "$NUM_PEERS" -lt 2 ]]; then
    fatal "A distributed cluster needs at least 2 nodes. Use 'single' for a standalone setup."
  fi

  if [[ "$ROLE" == "coordinator" ]]; then
    NODE_ID=0
    info "Coordinator is always node 0."
  else
    prompt NODE_ID_STR "This node's ID (coordinator=0, workers start at 1)" ""
    validate_integer "$NODE_ID_STR"
    NODE_ID="$NODE_ID_STR"
    [[ "$NODE_ID" -eq 0 ]] && fatal "NODE_ID 0 is reserved for the coordinator. Workers start at 1."
    [[ "$NODE_ID" -ge "$NUM_PEERS" ]] && fatal "NODE_ID ($NODE_ID) must be less than NUM_PEERS ($NUM_PEERS)."
  fi

  echo ""
  echo "Enter the internal IP of each node in order (node 0 = coordinator, then workers):"
  for ((i=0; i<NUM_PEERS; i++)); do
    local_label=""
    [[ "$i" -eq "$NODE_ID" ]] && local_label=" (this machine)"
    [[ "$i" -eq 0 && "$ROLE" == "coordinator" ]] && local_label=" (this machine — coordinator)"
    [[ "$i" -eq 0 && "$ROLE" != "coordinator" ]] && local_label=" (coordinator)"
    prompt IP_VAL "  Node ${i} internal IP${local_label}" ""
    validate_ip "$IP_VAL"
    PEER_IPS+=("$IP_VAL")
  done

  echo ""
fi

# ── step 3: index directory ───────────────────────────────────────────────────

echo -e "${BOLD}Step 3: Index Shards${RESET}"
DEFAULT_INDEX="${HOME}/avengine-index"
echo "Where are your pre-built .sfi index shard files on this machine?"
echo "(Copy them here with: scp /path/on/crawler/*.sfi user@this-vm:${DEFAULT_INDEX}/)"
echo ""
prompt INDEX_DIR "Path to index shard directory" "$DEFAULT_INDEX"

if [[ ! -d "$INDEX_DIR" ]]; then
  warn "Directory '$INDEX_DIR' does not exist. Creating it now."
  warn "Remember to copy your .sfi shards there before the search server can return results."
  mkdir -p "$INDEX_DIR"
fi
echo ""

# ── step 4: SSH public key ────────────────────────────────────────────────────

echo -e "${BOLD}Step 4: SSH Access${RESET}"
echo "An SSH public key lets you shell into the container (useful for debugging)."
echo ""

DETECTED_KEY=""
for candidate in "${HOME}/.ssh/id_ed25519.pub" "${HOME}/.ssh/id_rsa.pub"; do
  if [[ -f "$candidate" ]]; then
    DETECTED_KEY="$(cat "$candidate")"
    info "Detected key at $candidate"
    break
  fi
done

SSH_PUBKEY=""
if [[ -n "$DETECTED_KEY" ]]; then
  prompt_yn USE_DETECTED "Use the detected key above?" "y"
  if [[ "$USE_DETECTED" == "y" ]]; then
    SSH_PUBKEY="$DETECTED_KEY"
  else
    prompt SSH_PUBKEY "Paste your SSH public key (or leave blank to skip)" ""
  fi
else
  warn "No SSH key found at ~/.ssh/id_ed25519.pub or ~/.ssh/id_rsa.pub."
  prompt SSH_PUBKEY "Paste your SSH public key (or leave blank to skip)" ""
fi

if [[ -z "$SSH_PUBKEY" ]]; then
  warn "No SSH key set. You won't be able to shell into the container."
fi
echo ""

# ── step 5: nginx / SSL (coordinator only) ────────────────────────────────────

USE_NGINX="n"
CERT_PATH="/etc/letsencrypt/live/theavengine.com/fullchain.pem"

if [[ "$ROLE" == "coordinator" ]]; then
  echo -e "${BOLD}Step 5: Nginx + SSL (coordinator only)${RESET}"
  echo "nginx terminates HTTPS for theavengine.com and proxies to the search server."
  echo ""
  echo "Before saying yes, make sure:"
  echo "  1. A static external IP is assigned to this VM"
  echo "  2. The theavengine.com DNS A record points to that IP"
  echo "  3. GCP firewall allows TCP 80 and 443 inbound (from 0.0.0.0/0)"
  echo ""
  prompt_yn USE_NGINX "Set up nginx + SSL for theavengine.com?" "y"
  echo ""

  if [[ "$USE_NGINX" == "y" ]]; then
    # ── certbot / TLS certificate ─────────────────────────────────────────────
    if [[ -f "$CERT_PATH" ]]; then
      info "TLS certificate already exists — skipping certbot."
    else
      echo -e "${BOLD}Getting a TLS certificate via certbot${RESET}"
      echo "certbot will briefly listen on port 80 to prove domain ownership."
      warn "If anything else is using port 80 right now, stop it first."
      echo ""
      prompt_yn RUN_CERTBOT "Run certbot now to get the certificate?" "y"
      if [[ "$RUN_CERTBOT" == "y" ]]; then
        if ! command -v certbot &>/dev/null; then
          info "Installing certbot..."
          sudo apt-get update -qq
          sudo apt-get install -y certbot
        fi
        info "Running certbot — follow the prompts (enter your email when asked)..."
        sudo certbot certonly --standalone \
          -d theavengine.com -d www.theavengine.com
        echo ""
      else
        warn "Skipping certbot. nginx will fail to start without a cert at:"
        warn "  $CERT_PATH"
        warn "Run this manually before starting, then re-run setup.sh:"
        warn "  sudo certbot certonly --standalone -d theavengine.com -d www.theavengine.com"
        echo ""
      fi
    fi

    # ── auto-renewal cron (idempotent) ────────────────────────────────────────
    RENEW_JOB="0 3 * * * certbot renew --quiet && docker compose -f ${REPO_DIR}/deploy/docker-compose.nginx.yml exec nginx nginx -s reload"
    if sudo crontab -l 2>/dev/null | grep -qF "certbot renew"; then
      info "Certbot auto-renewal already in root crontab — skipping."
    else
      info "Adding certbot auto-renewal to root crontab (runs at 3am daily)..."
      ( sudo crontab -l 2>/dev/null; echo "$RENEW_JOB" ) | sudo crontab -
    fi
    echo ""
  fi
fi

# ── write .env.search ─────────────────────────────────────────────────────────

info "Writing ${ENV_FILE}..."

{
  echo "# Auto-generated by setup.sh — edit as needed"
  echo "# Re-run setup.sh to regenerate."
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

  echo ""
  echo "AVENGINE_HOST_INDEX_DIR=${INDEX_DIR}"

  if [[ -n "$SSH_PUBKEY" ]]; then
    echo "AVENGINE_SSH_PUBKEY=${SSH_PUBKEY}"
  fi

  if [[ "${#PEER_IPS[@]}" -gt 0 ]]; then
    echo ""
    echo "# Peer IP addresses (internal VPC IPs)"
    for i in "${!PEER_IPS[@]}"; do
      echo "PEER_${i}_ADDR=${PEER_IPS[$i]}"
    done
  fi
} > "$ENV_FILE"

echo "    Written to: ${ENV_FILE}"
echo ""

# ── install docker if needed ──────────────────────────────────────────────────

if ! command -v docker &>/dev/null; then
  info "Docker not found. Installing..."
  curl -fsSL https://get.docker.com | sudo sh
  sudo usermod -aG docker "${USER}"
  echo ""
  warn "Docker was just installed. You may need to log out and back in for group"
  warn "membership to take effect. If the next step fails with a permission error,"
  warn "run: newgrp docker"
  echo ""
fi

# ── build + start container ───────────────────────────────────────────────────

info "Building and starting the container (this may take a few minutes the first time)..."
echo ""

COMPOSE_CMD=(docker compose -f "$COMPOSE_SEARCH" --env-file "$ENV_FILE")
if [[ "$ROLE" == "coordinator" && "$USE_NGINX" == "y" ]]; then
  COMPOSE_CMD+=(-f "$COMPOSE_NGINX")
fi
COMPOSE_CMD+=(up -d --build)

"${COMPOSE_CMD[@]}"

echo ""

# ── summary ───────────────────────────────────────────────────────────────────

echo -e "${BOLD}╔══════════════════════════════════════════════════════╗${RESET}"
echo -e "${BOLD}║                    Setup Complete                    ║${RESET}"
echo -e "${BOLD}╚══════════════════════════════════════════════════════╝${RESET}"
echo ""
echo -e "  Role:         ${BOLD}${ROLE}${RESET} (NODE_ID=${NODE_ID}, NUM_PEERS=${NUM_PEERS})"
echo -e "  Index dir:    ${INDEX_DIR}"
echo -e "  Config file:  ${ENV_FILE}"
echo ""
echo "  Container commands:"
echo "    docker logs -f avengine-search      # stream logs"
if [[ "$USE_NGINX" == "y" ]]; then
  echo "    docker logs -f avengine-nginx       # nginx logs"
fi
echo "    ssh -p 2222 engine@localhost        # shell into container"
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
  echo ""
  echo "  See deploysetup.md for the gcloud firewall command."
  echo ""
fi

if [[ -z "$(ls -A "$INDEX_DIR" 2>/dev/null)" ]]; then
  warn "Index directory is empty. Copy your .sfi shards to ${INDEX_DIR}"
  warn "then restart the container:  docker compose -f infra/deploy/docker-compose.search.yml restart"
  echo ""
fi

echo "  See deploysetup.md for full documentation."
echo ""
