# GCP VM deployment

GitHub Actions deploys this repo to the AVEngine VM through Google Cloud
Workload Identity Federation and IAP SSH. The workflow uploads the checked-out
source archive to the VM, runs `infra/gcp/deploy-vm.sh`, builds the C++ binaries
on the VM, and starts native systemd services.

Required GitHub repository variables:

- `GCP_PROJECT_ID`
- `GCP_ZONE`
- `GCP_INSTANCE_NAME`
- `GCP_WORKLOAD_IDENTITY_PROVIDER`
- `GCP_SERVICE_ACCOUNT`

Required GitHub repository secret:

- `AVENGINE_SSH_PUBKEY`

Recommended variables:

- `AVENGINE_DOMAIN=theavengine.com`
- `AVENGINE_DATA_ROOT=/data`
- `AVENGINE_SEARCH_PORT=8080`

DNS should point `theavengine.com` and `www.theavengine.com` at the VM external
IP. The VM firewall must allow public TCP `80` and `443`, plus IAP SSH (`tcp:22`
from `35.235.240.0/20`). Team CLI access uses the host `engine` user over normal
SSH on port `22`.

The deploy script installs:

- Build/runtime packages (`cmake`, compiler toolchain, OpenSSL/Zlib headers,
  Python, profiling tools).
- Caddy on the host.
- An `engine` Linux user with `AVENGINE_SSH_PUBKEY` in
  `/home/engine/.ssh/authorized_keys`.
- `/opt/avengine/current` and `/avengine` symlinks for the active release.
- `/data` for persistent crawler, index, logs, and profiling state.
- `avengine-crawler.service`, `avengine-aggregate.service`, and
  `avengine-search.service`.
