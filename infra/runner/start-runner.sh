#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"

if [[ ! -f .env ]]; then
  echo "Missing infra/runner/.env. Copy .env.example and fill values."
  exit 1
fi

services=(
  github-runner
  github-runner-2
  github-runner-3
)

echo "Starting GitHub runner containers: ${services[*]}"
docker compose up -d "${services[@]}"

echo
docker compose ps "${services[@]}"
