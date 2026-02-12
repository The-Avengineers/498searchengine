#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"

if [[ ! -f .env ]]; then
  echo "Missing infra/runner/.env. Copy .env.example and fill values."
  exit 1
fi

docker compose up -d
docker compose ps
