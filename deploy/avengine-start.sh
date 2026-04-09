#!/usr/bin/env bash
set -euo pipefail

BIN_ROOT="${AVENGINE_BIN_ROOT:-/avengine/bin}"

"${BIN_ROOT}/start-crawler-swarm.sh"
"${BIN_ROOT}/start-aggregate-watch.sh"

echo "started managed AVEngine services"
