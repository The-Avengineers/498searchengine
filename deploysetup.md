# AVEngine Deployment Setup

Quick guide for teammates deploying the distributed search cluster on GCP.
No Docker — runs directly on the VM as a systemd service.

---

## Architecture

```
Internet  ──▶  theavengine.com  (DNS A → coordinator external IP)
                    │ HTTPS :443
                  nginx  (coordinator VM only, system service)
                    │ HTTP :8080
              search_server  (coordinator, NODE_ID=0, systemd service)
                    │ TCP :47857  (peer messages)
           ┌────────┴────────┐
     search_server       search_server
     (worker NODE_ID=1)  (worker NODE_ID=2)
         systemd             systemd
```

- **Coordinator** (Joshua's VM): nginx + search server. Fans every query to all workers and merges results.
- **Worker** (your VM): runs `search_server` in worker mode, serves your local index shards.
- Both roles auto-restart on crash and on VM reboot via systemd.

---

Josh 0 1
Tony 2
Mick 3
Jason 4
Mateo 5 6



## Prerequisites

1. **GCP VM** — `m4-highmem-16` (or `m4-hypermem-16`), Ubuntu 22.04+, 512 GB attached data disk.
2. **sudo access** — the default `ubuntu` user has this.
3. **Git** — `sudo apt-get install -y git` if not already present.
4. **Your index shards** (`.sfi` files) — see [Index Shards](#index-shards).
5. **TCP port 47857 open** between all nodes — see [Firewall](#firewall).

---

## Quick Start

```bash
git clone https://github.com/The-Avengineers/498searchengine.git
cd 498searchengine
./setup.sh
```

The script asks a few questions and handles everything else.

---

## What setup.sh Does

1. Asks your role (coordinator / worker / single) and cluster topology
2. Applies host kernel tuning (`/etc/sysctl.d/99-avengine.conf`)
3. Detects and mounts the 512 GB data disk at `/mnt/data`
4. Installs build deps (`build-essential cmake libssl-dev zlib1g-dev`)
5. Builds `search_server` from source (`cmake --build ... -j$(nproc)`)
6. Writes `/etc/avengine/search.env` with your role, node ID, and peer IPs
7. Installs and starts the `avengine-search` systemd service
8. *(Coordinator only)* installs nginx, runs certbot, and enables HTTPS

To re-run after changing IPs or config: just run `./setup.sh` again.

---

## Getting Peer IPs on GCP

The cluster uses internal VPC IPs for peer communication (not external IPs).

```bash
# From the VM itself
hostname -I | awk '{print $1}'
```

Or: GCP Console → Compute Engine → VM Instances → click instance → Network interfaces → **Primary internal IP**.

> **Note:** GCP VPCs don't support UDP broadcast, so auto-discovery doesn't work.
> `setup.sh` sets explicit `PEER_N_ADDR` env vars instead — that's why you need all the IPs.

---

## Index Shards

Each node holds its own slice of the index. The coordinator fans every query to all workers and merges results by score.

Copy shards from your crawler machine to the VM:
```bash
# From the crawler machine
scp /path/to/crawl-data/*.sfi ubuntu@<worker-vm-external-ip>:/mnt/data/avengine-index/
```

After copying, restart the service:
```bash
sudo systemctl restart avengine-search
```

---

## Firewall

All nodes need TCP port 47857 open inbound from each other's internal IPs.
Run once per project (replace IPs with your actual cluster IPs):

```bash
gcloud compute firewall-rules create avengine-peer \
  --allow tcp:47857 \
  --source-ranges=10.128.0.2/32,10.128.0.5/32,10.128.0.9/32 \
  --target-tags=avengine-node

# Tag each VM
gcloud compute instances add-tags <instance-name> --tags=avengine-node --zone=<zone>
```

---

## Coordinator Setup (Joshua's VM only)

Workers skip this section entirely — `setup.sh` only shows these steps when you choose role `coordinator`.

### Before running setup.sh:
1. **Reserve a static external IP:**
   ```bash
   gcloud compute addresses create avengine-ip --region=us-central1
   gcloud compute instances add-access-config <instance-name> \
     --access-config-name=external-nat \
     --address=$(gcloud compute addresses describe avengine-ip \
                   --region=us-central1 --format='value(address)')
   ```
2. **Point DNS:** Set the `theavengine.com` A record to that static IP.
3. **Open ports 80 + 443** in GCP firewall (needed for certbot and HTTPS traffic).

### During setup.sh:
Answer `coordinator` for role and `y` for nginx. The script installs nginx, runs `certbot --nginx` to get and configure the TLS cert, and enables both services.

---

## VM Hardware Settings (m4-highmem-16)

`setup.sh` applies all of this automatically.

### Kernel settings (`/etc/sysctl.d/99-avengine.conf`)

| Setting | Value | Why |
|---|---|---|
| `vm.swappiness` | `1` | Keeps index pages in RAM; avoids swapping under load |
| `fs.file-max` | `1048576` | Large indexes have hundreds of `.sfi` shards; default limit causes `EMFILE` |
| `net.core.somaxconn` | `65535` | Larger listen backlog for the HTTP server |
| `net.ipv4.tcp_tw_reuse` | `1` | Fast socket reuse for peer connections |

### Search server settings (`/etc/avengine/search.env`)

| Variable | Value | Why |
|---|---|---|
| `SEARCH_SHARD_WORKERS` | `14` | Parallel shard queries; matches 14 of the 16 vCPUs (2 reserved for OS) |

### systemd resource limits

| Limit | Value | Why |
|---|---|---|
| `LimitNOFILE` | `1048576` | File handles for index shards |
| `LimitMEMLOCK` | `infinity` | Allows mlock to pin index pages in RAM |

---

## Restartability

The `avengine-search` systemd service is configured to restart automatically:

| Setting | Value | Meaning |
|---|---|---|
| `Restart` | `on-failure` | Restarts if the process crashes or is killed by a signal |
| `RestartSec` | `5s` | Waits 5 seconds before each restart attempt |
| `StartLimitBurst` | `5` | Gives up after 5 failures in 2 minutes (prevents a crash loop) |

The service is also `enabled`, so it starts automatically when the VM boots.

To check the restart count:
```bash
sudo systemctl status avengine-search
```

---

## Verifying the Cluster

```bash
# Search API on this node
curl "http://localhost:8080/api/search?q=hello"

# From the coordinator, check it reached workers
curl "http://localhost:8080/api/search?q=hello&verbose=1"

# Check service health
sudo systemctl status avengine-search
```

---

## Common Commands

```bash
# Service management
sudo systemctl status avengine-search      # current state + recent logs
sudo systemctl restart avengine-search     # restart manually
sudo systemctl stop avengine-search        # stop
sudo systemctl start avengine-search       # start

# Logs
sudo journalctl -fu avengine-search        # stream live logs
sudo journalctl -u avengine-search --since "10 min ago"

# Edit config (peer IPs, shard workers, etc.)
sudo nano /etc/avengine/search.env
sudo systemctl restart avengine-search

# Rebuild after a code change
cd ~/498searchengine
git pull
cmake --build build --target search_server -j$(nproc)
sudo systemctl restart avengine-search
```

---

## Troubleshooting

**Service won't start**
```bash
sudo journalctl -u avengine-search --since "5 min ago"
```
Most common causes: index directory missing, bad env file syntax, binary not built.

**Search returns no results**
```bash
ls /mnt/data/avengine-index/*.sfi | wc -l   # should be > 0
sudo systemctl restart avengine-search
```

**Workers not connecting to coordinator**
```bash
# Test connectivity from worker to coordinator
nc -zv <coordinator-internal-ip> 47857

# Check peer IPs in config
cat /etc/avengine/search.env | grep PEER
```

**Hit the restart limit (service stopped retrying)**
```bash
# Reset the failure counter and try again
sudo systemctl reset-failed avengine-search
sudo systemctl start avengine-search
```

**Rebuild needed after git pull**
```bash
cmake --build ~/498searchengine/build --target search_server -j$(nproc)
sudo systemctl restart avengine-search
```

---

For advanced topics (crawler setup, profiling, GitHub Actions runner), see `docs/deployment.md`.
