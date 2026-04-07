# search_server

## What it does

`search_server` now runs in two roles:

- `coordinator` - exposes `GET /api/search`, fans out to peer workers, merges the responses, and returns one global page window
- `worker` - loads its local shard directory, listens on the node mesh, and answers search requests from the coordinator

The same binary is used in both roles. The role is selected with `SEARCH_ROLE`.

## Environment

Common environment variables:

- `SEARCH_ROLE=coordinator|worker`
- `NODE_ID` - this node's 0-based id in the peer set
- `NUM_PEERS` - total number of peers in the cluster
- `INDEX_DIR` - directory containing this node's local `.sfi` files

If `SEARCH_ROLE` is not set, the binary defaults to `coordinator`.

## Build and run

Local single-node search:

```bash
cmake --build build --target search_server -j 6
./build/search_server 8080 .
```

Distributed search on multiple machines:

```bash
# coordinator
SEARCH_ROLE=coordinator NODE_ID=0 NUM_PEERS=3 INDEX_DIR=/path/to/coordinator/index ./build/search_server 8080 /path/to/coordinator/index

# worker 1
SEARCH_ROLE=worker NODE_ID=1 NUM_PEERS=3 INDEX_DIR=/path/to/worker1/index ./build/search_server 8081 /path/to/worker1/index

# worker 2
SEARCH_ROLE=worker NODE_ID=2 NUM_PEERS=3 INDEX_DIR=/path/to/worker2/index ./build/search_server 8082 /path/to/worker2/index
```

All peers must be able to discover each other on the local network or a VPN that supports broadcast traffic.

## API shape

```http
GET /api/search?q=apple&page=1&limit=10
```

Response fields:

- `query`
- `page`
- `limit`
- `total_hits`
- `took_ms`
- `errors`
- `results`

Each result includes:

- `url`
- `title`
- `score`

`page` is 1-based. The public HTTP API still clamps `limit` to `10` for compatibility, but the coordinator overfetches internally before merging worker responses.

## Quick checks

```bash
curl -i 'http://127.0.0.1:8080/api/search?q=apple&page=1&limit=10'
curl -i 'http://127.0.0.1:8080/api/search?q=apple&page=2&limit=10'
```

## Notes

- Only `GET` and `OPTIONS` are supported.
- A blank query returns an empty result set.
- The coordinator returns partial results when a worker times out or is unreachable.
