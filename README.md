<p align="center">
  <img src="frontend/media/logo-optimized-800.png" alt="AVEngine" width="640">
</p>

# AVEngine

AVEngine is a web search engine with:

- a C++ search server and ranking pipeline
- a crawler and indexer that produce `.sfi` shard files
- a browser frontend served by the search server
- a Node CLI in [`search-cli/`](search-cli/README.md)

Main public site:

- `https://theavengine.com/`

## CLI

A terminal UI for search, crawler management, status, profiling, and opening the live site is available in [`search-cli/`](search-cli/README.md).

```bash
cd search-cli
npm install
npm run build
npm link

avengine
avengine query "spider man" --verbose
avengine lucky "apple"
avengine stats
avengine status
avengine open
```

Notes:

- `avengine open` opens the configured AVEngine site. It does not start a local search server.
- `AVENGINE_SERVER_URL` defaults to `https://theavengine.com`.

## Quick checks

Public site:

```bash
curl -i 'https://theavengine.com/'
curl -s 'https://theavengine.com/api/search?q=apple&limit=5' | python3 -m json.tool
```

Local server:

```bash
curl -i 'http://127.0.0.1:8080/'
curl -s 'http://127.0.0.1:8080/api/search?q=apple&limit=5' | python3 -m json.tool
```

## Local single-node search

Build the server:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target search_server -j 4
```

Run against a local shard directory, for example `example-indices` if you have it:

```bash
unset SEARCH_ROLE NODE_ID NUM_PEERS INDEX_DIR
./build/search_server 8080 example-indices
```

Then open:

- `http://127.0.0.1:8080/`

Or query directly:

```bash
curl -s 'http://127.0.0.1:8080/api/search?q=apple&limit=10' | python3 -m json.tool
```

If you want to use a different shard directory:

```bash
./build/search_server 8080 /path/to/index-dir
```

## GCP VM setup

The current repo includes a VM setup script:

```bash
./setup.sh
```

It builds `search_server`, writes `/etc/avengine/search.env`, installs the `avengine-search` systemd service, and can optionally configure nginx + TLS on the coordinator.

Current roles:

- `single` - standalone local serving, no peers
- `coordinator` - serves HTTP and fans out search requests
- `worker` - joins the peer mesh and answers coordinator requests

Important operational notes:

- Only the coordinator serves the browser frontend and `/api/search` over HTTP.
- Workers do not expose the search UI on `:8080`; they serve the coordinator over the peer mesh.
- The current setup script is pinned to the checked-in three-VM cluster defaults in `setup.sh`.

For deployment details, see [`docs/deployment.md`](docs/deployment.md).

## Distributed search

`search_server` supports coordinator/worker search across multiple machines.

Build once:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target search_server -j 4
```

Coordinator:

```bash
SEARCH_ROLE=coordinator \
NODE_ID=0 \
NUM_PEERS=3 \
INDEX_DIR=/path/to/coordinator-index \
./build/search_server 8080 /path/to/coordinator-index
```

Worker 1:

```bash
SEARCH_ROLE=worker \
NODE_ID=1 \
NUM_PEERS=3 \
INDEX_DIR=/path/to/worker1-index \
./build/search_server 8080 /path/to/worker1-index
```

Worker 2:

```bash
SEARCH_ROLE=worker \
NODE_ID=2 \
NUM_PEERS=3 \
INDEX_DIR=/path/to/worker2-index \
./build/search_server 8080 /path/to/worker2-index
```

All nodes must share the same `peer_list.txt` in the working directory. Each line is:

```text
<node-id> <ip-or-hostname>
```

Current peer transport details:

- peer TCP port: `46857`
- coordinator is the only node you open in a browser
- workers answer search requests from the coordinator
- `SEARCH_LOAD_TIMEOUT_SEC` can be used to bound startup shard loading; in distributed mode the server defaults to `30` seconds if unset

## Search API

Endpoint:

- `GET /api/search`

Parameters:

- `q` - query text
- `page` - 1-based page number, default `1`
- `limit` - page size, clamped to `1..10`

Example:

```text
https://theavengine.com/api/search?q=spider-man&page=1&limit=10
```

Response fields include:

- `total_hits`
- `took_ms`
- `parse_ms`
- `solve_ms`
- `collect_ms`
- `rank_ms`
- `errors`
- `results`

For the full API and query syntax, see [`docs/search_server.md`](docs/search_server.md).

## Query syntax

Supported query forms:

- single term: `apple`
- implicit AND: `apple banana`
- phrase: `"apple pie"`
- OR: `apple | banana`
- negation: `brown -fox`

## Repo pointers

- [`docs/search_server.md`](docs/search_server.md) - search server behavior, API, query syntax
- [`docs/deployment.md`](docs/deployment.md) - cluster and site deployment
- [`docs/crawler_usage.md`](docs/crawler_usage.md) - crawler runtime and corpus management
- [`profiling/README.md`](profiling/README.md) - local and CI profiling
- [`search-cli/README.md`](search-cli/README.md) - CLI usage
