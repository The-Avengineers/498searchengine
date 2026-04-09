#!/usr/bin/env bash
set -euo pipefail

cat <<'EOF'
AVEngine container

Layout:
  /avengine   binaries and project assets
  /config     seed and block lists
  /data       persistent runtime state
  /logs       crawler and search logs
  /crawlers   per-worker crawl outputs
  /corpus     live merged shard directory
  /profiling  profiling scripts and fixtures

Commands:
  av-help             Show this help
  av-status           Show crawler, aggregate, and search status
  av-start            Start crawler swarm and aggregate watcher
  av-stop             Stop search server, aggregate watcher, and crawler swarm
  av-serve [port] [index-dir] [--daemon]
                      Start search server on the given port
  av-stop-search      Stop background search server
  av-stop-crawlers    Stop crawler swarm
  av-start-aggregate  Start aggregate watcher
  av-stop-aggregate   Stop aggregate watcher

Examples:
  av-status
  av-serve 8080
  av-serve 8080 /corpus --daemon
EOF
