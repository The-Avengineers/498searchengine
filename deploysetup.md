# AVEngine Deployment Setup

Quick guide for teammates deploying the distributed search cluster on GCP.

---

## Architecture

```
Internet  ──▶  theavengine.com  (DNS A → coordinator external IP)
                    │ HTTPS :443
                  nginx  (coordinator VM only)
                    │ HTTP :8080
              search_server  (coordinator, NODE_ID=0)
                    │ TCP :47857  (peer messages)
           ┌────────┴────────┐
     search_server       search_server
     (worker, NODE_ID=1)  (worker, NODE_ID=2)
```

- **Coordinator** (Joshua's VM): runs nginx + search server. Fans queries out to all workers and merges results.
- **Worker** (your VM): serves your local index shards. Answers queries from the coordinator.
- Same Docker image on every node — behavior is controlled by env vars.

---

## Prerequisites

Before running `setup.sh` you need:

1. **A GCP VM** running Ubuntu 22.04+. `e2-standard-2` or better recommended.
2. **Your index shards** (`.sfi` files) copied to the VM (see [Index Shards](#index-shards)).
3. **An SSH keypair** on the VM (`~/.ssh/id_ed25519` or `id_rsa`). Generate one with `ssh-keygen` if you don't have one.
4. **TCP port 47857 open** in the GCP firewall between all nodes (see [Firewall](#firewall)).
5. **Git** installed (`sudo apt-get install -y git`).

---

## Quick Start

```bash
git clone https://github.com/The-Avengineers/498searchengine.git
cd 498searchengine
./setup.sh
```

The script asks you a few questions and does the rest. Most teammates just need to answer:
- Role: `worker`
- Number of nodes: (ask Joshua — usually 2 or 3)
- Your node ID: (ask Joshua — workers start at 1)
- Peer IPs: your VM's internal IP + the coordinator's internal IP
- Index directory: where you put your `.sfi` files (default: `~/avengine-index`)

---

## What setup.sh Does

1. Asks 4–6 questions about your role and the cluster topology
2. Writes a `.env.search` config file in the repo root
3. Installs Docker if it isn't already installed
4. Runs `docker compose up --build` to build the image and start the container
5. Prints a summary with log/SSH commands and a health check

To re-run setup (e.g. after changing IPs): just run `./setup.sh` again. It overwrites `.env.search` and restarts the container.

---

## Getting Peer IPs on GCP

The cluster uses internal VPC IPs (not external IPs) for peer communication. To find them:

1. GCP Console → Compute Engine → VM Instances
2. Click your instance → **Network interfaces** → **Primary internal IP**

Or from the VM itself:
```bash
hostname -I | awk '{print $1}'
```

> **Note:** GCP VPCs do not support UDP broadcast, so the default auto-discovery won't work. `setup.sh` configures explicit `PEER_N_ADDR` env vars instead — that's why you need all the IPs.

---

## Index Shards

Each node holds its own slice of the index. The coordinator fans every query to all workers and merges results by score.

**Copy shards from your crawler machine to the VM:**
```bash
# From your crawler machine
scp /path/to/crawl-data/*.sfi your-user@<worker-vm-external-ip>:~/avengine-index/
```

**Or from inside the VM if you have the shards elsewhere:**
```bash
cp /wherever/*.sfi ~/avengine-index/
```

The container mounts `~/avengine-index` (or whatever you told `setup.sh`) as read-only at `/avengine-index`. The search server picks it up automatically on start.

If you add shards after the container is already running, restart it:
```bash
docker compose -f infra/deploy/docker-compose.search.yml restart
```

---

## Firewall

All nodes need TCP port 47857 open inbound from each other's internal IPs. Run this once per project (adjust IPs to match your cluster):

```bash
gcloud compute firewall-rules create avengine-peer \
  --allow tcp:47857 \
  --source-ranges=10.128.0.2/32,10.128.0.5/32,10.128.0.9/32 \
  --target-tags=avengine-node
```

Then add the `avengine-node` network tag to each VM:
```bash
gcloud compute instances add-tags <instance-name> --tags=avengine-node --zone=<zone>
```

---

## Coordinator Setup (Joshua's VM only)

Workers skip this section. The coordinator has extra one-time steps.

### Reserve a static external IP
```bash
gcloud compute addresses create avengine-ip --region=us-central1
gcloud compute instances add-access-config <instance-name> \
  --access-config-name=external-nat \
  --address=$(gcloud compute addresses describe avengine-ip --region=us-central1 --format='value(address)')
```

### Point DNS
Set an A record for `theavengine.com` → the static external IP above.

### Get a TLS certificate (before starting nginx)
```bash
sudo apt-get install -y certbot
sudo certbot certonly --standalone -d theavengine.com -d www.theavengine.com
```

Add auto-renewal to root crontab (`sudo crontab -e`):
```
0 3 * * * certbot renew --quiet && docker compose -f /home/ubuntu/498searchengine/deploy/docker-compose.nginx.yml exec nginx nginx -s reload
```

### Run setup.sh
Choose role `coordinator` and answer `y` to the nginx question. The script starts both the search server and nginx.

---

## Verifying the Cluster

**Check the search API on your node:**
```bash
curl "http://localhost:8080/api/search?q=hello"
```

**From the coordinator, check it reaches workers:**
```bash
curl "http://localhost:8080/api/search?q=hello&verbose=1"
# Response includes per-node timing breakdown
```

**Check the container is running:**
```bash
docker ps | grep avengine-search
```

---

## Common Commands

```bash
# Stream container logs
docker logs -f avengine-search

# Shell into the container (requires SSH key configured)
ssh -p 2222 engine@localhost

# Restart just the search server (without rebuilding)
docker compose -f infra/deploy/docker-compose.search.yml restart

# Stop everything
docker compose -f infra/deploy/docker-compose.search.yml down

# Rebuild and restart after a code change
docker compose -f infra/deploy/docker-compose.search.yml up -d --build

# Inside the container — useful shortcuts
av-status     # show running processes
av-stats      # live stats (docs indexed, memory)
av-serve 8080 /avengine-index --daemon   # restart search server manually
```

---

## Troubleshooting

**Container won't start**
```bash
docker logs avengine-search
```
Most common causes: index directory missing or not set, SSH pubkey not set (just a warning, not fatal), permissions issue on the index directory.

**Search returns no results**
- Check the index directory has `.sfi` files: `ls ~/avengine-index/`
- Check the search server loaded them: `docker logs avengine-search | grep -i shard`

**Workers not connecting to coordinator**
- Verify firewall: `nc -zv <peer-ip> 47857` from each node
- Confirm `PEER_N_ADDR` values in `.env.search` match actual internal IPs
- Check coordinator logs: `docker logs avengine-search | grep peer`

**Permission denied running Docker**
```bash
# Add yourself to the docker group (then log out and back in)
sudo usermod -aG docker $USER
newgrp docker
```

**Want to change configuration**
Re-run `./setup.sh`. It overwrites `.env.search` and restarts the container. Or edit `.env.search` directly and run:
```bash
docker compose -f infra/deploy/docker-compose.search.yml up -d
```

---

For advanced configuration (profiling, crawler setup, GitHub Actions runner), see `docs/deployment.md`.
