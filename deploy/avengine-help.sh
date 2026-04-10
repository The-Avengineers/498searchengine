#!/usr/bin/env bash
set -euo pipefail

cat <<'EOF'
AVEngine container

Layout:
  /avengine   binaries and project assets
  /config     seed and block lists
  /data       persistent runtime state
              crawler and aggregate services stop at AVENGINE_MAX_DATA_BYTES (default 48 GiB)
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
  av-profile [output-dir] [auto|fixture|corpus]
                      Run crawler + search profiling inside the container
                      Live corpus mode waits 60s, refreshes /corpus, then profiles with live queries
  av-serve [port] [index-dir] [--daemon]
                      Start search server on the given port
  av-stop-search      Stop background search server
  av-stop-crawlers    Stop crawler swarm
  av-start-aggregate  Start aggregate watcher
  av-stop-aggregate   Stop aggregate watcher
  av-promote-corpus-profile
                      Snapshot /data/corpus-index/current into /data/corpus-profile/current

Examples:
  av-status
  av-stats
  av-watch 2
  av-profile
  av-profile /data/profiling corpus
  av-promote-corpus-profile
  av-serve 8080
  av-serve 8080 /corpus --daemon
EOF
