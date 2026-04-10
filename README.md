# 498searchengine

## How To Run

### Local single-node search

Build the server:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target search_server -j 4
```

Run the search engine against the sample indices:

```bash
./build/search_server 8080 example-indices
```

Open the frontend:

```text
http://127.0.0.1:8080/
```

The frontend and API are served from the same origin on current `master`.

### Quick API checks

```bash
curl -i 'http://127.0.0.1:8080/'
curl -s 'http://127.0.0.1:8080/api/search?q=apple&limit=5' | python3 -m json.tool
curl -i 'http://127.0.0.1:8080/api/search?q=apple&page=1&limit=10'
curl -i 'http://127.0.0.1:8080/api/search?q=apple&page=2&limit=10'
```

### Use a different index directory

If you have your own `.sfi` shard directory, pass it as the second argument:

```bash
./build/search_server 8080 /path/to/index-dir
```

### Distributed search

Current `master` supports coordinator/worker search across multiple machines.

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
./build/search_server 8081 /path/to/worker1-index
```

Worker 2:

```bash
SEARCH_ROLE=worker \
NODE_ID=2 \
NUM_PEERS=3 \
INDEX_DIR=/path/to/worker2-index \
./build/search_server 8082 /path/to/worker2-index
```

Notes:

- The coordinator is the only node you open in a browser.
- Workers answer search requests over the node mesh.
- Peers must be on the same LAN or on a VPN that supports the current broadcast-based discovery.
- The current peer mesh uses fixed transport ports, so the distributed cluster is intended to run with one peer process per machine.

### Query API

`GET /api/search`

Parameters:

- `q`: query text
- `page`: 1-based page number, default `1`
- `limit`: page size, clamped to `10`

Example:

```text
http://127.0.0.1:8080/api/search?q=spider-man&page=1&limit=10
```

For more detail, see [`docs/search_server.md`](docs/search_server.md).
