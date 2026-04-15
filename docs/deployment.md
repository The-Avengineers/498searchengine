# Deployment Guide

This doc covers deploying the search cluster on GCP VMs and serving it at theavengine.com.

---

## Architecture

```
Internet  →  theavengine.com (DNS A → coordinator external IP)
               ↓ HTTPS 443
            nginx (coordinator VM)
               ↓ proxy
            search_server :8080  (coordinator, NODE_ID=0)
               ↓ TCP :47857 (peer messages)
            search_server (worker VMs, NODE_ID=1,2,...)
```

- **Coordinator VM** (Joshua's): nginx + search_server + frontend
- **Worker VMs** (teammates): search_server only
- All nodes run the same Docker image; behavior is controlled by env vars

---

## Peer Discovery on GCP

The default peer discovery uses UDP broadcast, which **does not work on GCP VPCs**. Instead, set `PEER_N_ADDR` env vars with each node's internal IP:

```
PEER_0_ADDR=10.128.0.2   # coordinator
PEER_1_ADDR=10.128.0.5   # worker 1
PEER_2_ADDR=10.128.0.9   # worker 2
```

When any `PEER_N_ADDR` var is set, the node connects directly and skips UDP broadcast entirely.

---

## One-Time Coordinator Setup

### 1. DNS
Point the `theavengine.com` A record to the coordinator VM's **external static IP**.

### 2. Reserve a static external IP (GCP)
```bash
gcloud compute addresses create avengine-ip --region=us-east1
gcloud compute instances add-access-config <instance-name> \
  --access-config-name=external-nat \
  --address=$(gcloud compute addresses describe avengine-ip --region=us-east1 --format='value(address)')
```

### 3. Get TLS certificate (on coordinator VM, before starting nginx)
```bash
sudo apt-get install -y certbot
sudo certbot certonly --standalone -d theavengine.com -d www.theavengine.com
```

Auto-renew (add to root crontab):
```
0 3 * * * certbot renew --quiet && docker compose -f /path/to/docker-compose.nginx.yml exec nginx nginx -s reload
```

### 4. Open GCP firewall ports

On each VM, open the peer TCP port to the other VMs' internal IPs:
```bash
gcloud compute firewall-rules create avengine-peer \
  --allow tcp:47857 \
  --source-ranges=10.128.0.2/32,10.128.0.5/32,10.128.0.9/32 \
  --target-tags=avengine-node
```

Port 8080 only needs to be reachable from nginx (same VM, same Docker network — no firewall rule needed).

---

## Running the Coordinator

Create `.env.search` in the repo root:
```
SEARCH_ROLE=coordinator
NODE_ID=0
NUM_PEERS=3
AVENGINE_HOST_INDEX_DIR=/path/to/index/shards
AVENGINE_SSH_PUBKEY=<your public key>
PEER_0_ADDR=10.128.0.2
PEER_1_ADDR=10.128.0.5
PEER_2_ADDR=10.128.0.9
```

Start the search server + nginx:
```bash
docker compose \
  -f infra/deploy/docker-compose.search.yml \
  -f deploy/docker-compose.nginx.yml \
  --env-file .env.search \
  up -d --build
```

---

## Adding a Worker VM (teammates)

Copy the repo to your GCP VM and run the setup script:

```bash
git clone https://github.com/The-Avengineers/498searchengine.git
cd 498searchengine

# Args: NODE_ID  NUM_PEERS  PEER_0_IP  PEER_1_IP  PEER_2_IP ...
./deploy/setup-worker-vm.sh 1 3 10.128.0.2 10.128.0.5 10.128.0.9
```

The script:
1. Installs Docker if needed
2. Writes `.env.search` with your node ID, peer IPs, and index dir
3. Builds and starts the container

Then copy your index shards to `~/avengine-index/` (or wherever `AVENGINE_HOST_INDEX_DIR` points).

---

## Index Distribution

Each node should have its own slice of the index. The coordinator fans out every query to all workers and merges results by score — so each node only needs to hold the shards it crawled.

Copy shards from your crawler machine to the VM:
```bash
scp /path/to/crawl-data/*.sfi user@worker-vm:~/avengine-index/
```

---

## Useful Commands

```bash
# Check container logs
docker logs -f avengine-search

# SSH into container
ssh -p 2222 engine@localhost

# Restart search server inside container
ssh -p 2222 engine@localhost av-serve 8080 /data/index --daemon

# Test search API
curl "http://localhost:8080/api/search?q=test"
```

---

## Single-Node (No Distribution)

To run everything on one machine (no workers):
```bash
AVENGINE_HOST_INDEX_DIR=/path/to/index \
AVENGINE_SSH_PUBKEY="$(cat ~/.ssh/id_ed25519.pub)" \
docker compose -f infra/deploy/docker-compose.search.yml up -d --build
```

`NUM_PEERS` defaults to 1, so no peer communication happens.
