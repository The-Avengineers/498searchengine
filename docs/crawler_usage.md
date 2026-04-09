cmake -S . -B build
cmake --build build --target crawler_main
./build/crawler_main

## SSH crawler box

The deployed Docker container now acts as a long-lived crawler box:

- `sshd` stays available for manual inspection
- multiple `crawler_main` processes start automatically in the background
- each worker writes to `/var/lib/avengine/crawlers/<id>`
- the merged corpus lives at `/var/lib/avengine/corpus-index/current`

Common SSH-side commands:

```bash
ps -ef | grep crawler_main
ls /var/lib/avengine/logs
find /var/lib/avengine/corpus-index/current -maxdepth 1 -name '*.sfi' | wc -l
/opt/avengine/bin/search_server 8080 /var/lib/avengine/corpus-index/current
```

Swarm helper scripts:

```bash
/opt/avengine/bin/start-crawler-swarm.sh
/opt/avengine/bin/start-crawler-swarm.sh --reset
/opt/avengine/bin/stop-crawler-swarm.sh
/opt/avengine/bin/aggregate-corpus-index.sh
```
