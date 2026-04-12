cmake -S . -B build
cmake --build build --target crawler_main
./build/crawler_main

## SSH crawler box

The deployed Docker container now acts as a long-lived crawler box:

- `sshd` stays available for manual inspection
- multiple `crawler_main` processes start automatically in the background
- each worker writes to `/var/lib/avengine/crawlers/<id>`
- the merged corpus lives at `/var/lib/avengine/corpus-index/current`
- the promoted profiling corpus lives at `/var/lib/avengine/corpus-profile/current`
- SSH access is granted through `AVENGINE_SSH_PUBKEY` when the container starts
- crawler and aggregate work stop once `/data` reaches `AVENGINE_MAX_DATA_BYTES` (defaults to 128 GiB)
- the default crawler scale is 16 `crawler_main` processes with 32 crawl threads each
- the GitHub Actions runner that hosts this service should have a dedicated label such as `avengine-runtime`

The compose deploy stores `/data` on the host at `/mnt/data1/avengine/data` by default. Use the
normal host Docker Engine (`DOCKER_CONTEXT=default`) for this deploy; Docker Desktop blocks `/mnt`
bind mounts unless the path is manually added to its file-sharing allowlist. Override
`AVENGINE_HOST_DATA_ROOT` if the runtime data should live on a different mounted drive.

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
av-live-profile 30 crawler
av-start
av-start-crawlers
av-stop
av-serve 8080
av-serve 8080 /corpus --daemon
av-stop-search
```

To enable SSH access on the runtime box, set `AVENGINE_SSH_PUBKEY` to one or more newline-separated public keys before `docker compose up`. The default compose file no longer bind-mounts `infra/deploy/authorized_keys`, which avoids Docker Desktop file-sharing failures on the self-hosted runner.

For local deploys, `./run-container.sh up` uses `DOCKER_CONTEXT=default` and includes the profiling
compose overlay by default. Set `AVENGINE_ENABLE_LIVE_PROFILING=0` to start without the extra
tracing privileges and tracefs mounts.

The intended operating model is:

- `deploy-avengine.yml` deploys or refreshes the long-lived runtime service on the dedicated `avengine-runtime` runner
- the deploy workflow prepares `AVENGINE_HOST_DATA_ROOT` before starting the container
- the crawler supervisor and aggregate watcher keep running in the background on that host
- `live-internet-crawl.yml` no longer boots or tears down the service; it refreshes the aggregated corpus, promotes `/var/lib/avengine/corpus-profile/current`, and profiles that snapshot
- `profiling.yml` reads only the promoted snapshot from the same dedicated runtime runner

`av-profile ... corpus` still profiles the live `/corpus` view. CI `corpus-profile` instead reuses the promoted snapshot at `/var/lib/avengine/corpus-profile/current`.

Swarm helper scripts:

```bash
av-start
/avengine/bin/start-crawler-swarm.sh --reset
av-stop-crawlers
av-start-aggregate
av-stop-aggregate
av-promote-corpus-profile
/avengine/bin/aggregate-corpus-index.sh
```
