# GCP VM deployment

GitHub Actions deploys this repo to the AVEngine VM through Google Cloud
Workload Identity Federation and IAP SSH. The workflow uploads the checked-out
source archive to the VM, runs `infra/gcp/deploy-vm.sh`, and starts the Docker
Compose stack under `/opt/avengine/current`.

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
- `AVENGINE_SSH_PORT=22222`
- `AVENGINE_HTTP_PORT=127.0.0.1:8080`
- `AVENGINE_HOST_DATA_ROOT=/mnt/data1/avengine/data`

DNS should point `theavengine.com` and `www.theavengine.com` at the VM external
IP. The VM firewall must allow public TCP `80` and `443`, the chosen runtime SSH
port for CLI management, and IAP SSH (`tcp:22` from `35.235.240.0/20`).
