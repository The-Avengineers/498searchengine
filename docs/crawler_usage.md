cmake -S . -B build
cmake --build build --target crawler_main
./build/crawler_main

## SSH crawler box

The deployed Docker container now acts as a long-lived crawler box:

- `sshd` stays available for manual inspection
- multiple `crawler_main` processes start automatically in the background
- each worker writes to `/var/lib/avengine/crawlers/<id>`
- the merged corpus lives at `/var/lib/avengine/corpus-index/current`
- teammates can be granted access by adding their public keys to `infra/deploy/authorized_keys`

When you SSH in, the container exposes a simplified layout at the filesystem root:

- `/avengine` for binaries and project assets
- `/config` for seed and block lists
- `/data` for persistent runtime state
- `/logs` for crawler and search logs
- `/crawlers` for per-worker crawl outputs
- `/corpus` for the live merged shard directory
- `/profiling` for profiling scripts and fixtures

Common SSH-side commands:

```bash
av-help
av-status
av-stats
av-watch 2
av-profile
AVENGINE_LIVE_PROFILE_WAIT_SECONDS=60 av-profile /data/profiling corpus
av-start
av-start-crawlers
av-stop
av-serve 8080
av-serve 8080 /corpus --daemon
av-stop-search
```

Swarm helper scripts:

```bash
av-start
/avengine/bin/start-crawler-swarm.sh --reset
av-stop-crawlers
av-start-aggregate
av-stop-aggregate
/avengine/bin/aggregate-corpus-index.sh
```
