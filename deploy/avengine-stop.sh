#!/usr/bin/env bash
set -euo pipefail

BIN_ROOT="${AVENGINE_BIN_ROOT:-/avengine/bin}"

"${BIN_ROOT}/stop-search-server.sh"
"${BIN_ROOT}/stop-aggregate-watch.sh"
"${BIN_ROOT}/stop-crawler-swarm.sh" --quiet

echo "stopped managed AVEngine services"
