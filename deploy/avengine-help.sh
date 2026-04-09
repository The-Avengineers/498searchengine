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
  av-stats            Show per-worker crawl stats
  av-watch [seconds]  Refresh status and stats every few seconds
  av-start            Start crawler supervisor and aggregate watcher
  av-stop             Stop search server, aggregate watcher, crawler supervisor, and crawler swarm
  av-serve [port] [index-dir] [--daemon]
                      Start search server on the given port
  av-stop-search      Stop background search server
  av-stop-crawlers    Stop crawler swarm
  av-start-aggregate  Start aggregate watcher
  av-stop-aggregate   Stop aggregate watcher

Examples:
  av-status
  av-stats
  av-watch 2
  av-serve 8080
  av-serve 8080 /corpus --daemon
EOF
