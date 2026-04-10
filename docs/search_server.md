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
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target search_server -j 4
./build/search_server <port> <index-dir>
```

| Argument | Default | Description |
|----------|---------|-------------|
| `port` | `8080` | TCP port to listen on |
| `index-dir` | `.` | Directory containing `.sfi` shard files |

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

## API

### `GET /api/search`

Query parameters:

| Parameter | Required | Description |
|-----------|----------|-------------|
| `q` | yes | Search query (URL-encoded) |
| `page` | no | 1-based page number (default 1) |
| `limit` | no | Max results per page (clamped to 1–10, default 10) |

Response:

```json
{
  "query": "apple",
  "page": 2,
  "limit": 10,
  "total_hits": 3,
  "took_ms": 2,
  "parse_ms": 0,
  "solve_ms": 1,
  "collect_ms": 1,
  "rank_ms": 0,
  "errors": [],
  "results": [
    {
      "url": "https://example.com/page",
      "title": "Apple Result",
      "score": 1.234
    }
  ]
}
```

Timing fields:

| Field | Description |
|-------|-------------|
| `took_ms` | Total server-side time from request receipt to response |
| `parse_ms` | Query parsing (lexer + AST construction) |
| `solve_ms` | Constraint solver across all shards (ISR traversal) |
| `collect_ms` | ISR position collection for ranking signals |
| `rank_ms` | Scoring and top-K selection |

`page` is 1-based. The coordinator overfetches internally before merging worker responses.

### Query syntax

| Syntax | Example | Meaning |
|--------|---------|---------|
| Single term | `apple` | Documents containing "apple" |
| AND | `apple banana` | Documents containing both terms |
| Phrase | `"apple pie"` | Documents containing the exact phrase |
| OR | `apple OR banana` | Documents containing either term |

Tokenization note:

- Internal hyphens and apostrophes are treated as separators inside a phrase-like token, so `Spider-man` behaves like `"spider man"` and `Joe's Pizza` behaves like `"joe s" pizza`.
- A leading `-` still means negation, so `brown -fox` keeps its current Boolean meaning.


### Error response

Parse errors include an `offset` pointing into the query string:

```json
{
  "query": "\"unterminated phrase",
  "errors": [{ "offset": 0, "message": "unterminated phrase" }],
  "results": []
}
```

Timeout:

```json
{
  "query": "common term",
  "errors": [{ "offset": 0, "message": "query timed out after exceeding 500 ms threshold" }],
  "results": []
}
```

### Other endpoints

| Path | Response |
|------|----------|
| `/`, `/results.html`, and frontend assets | `200 OK` when file exists |
| Anything outside `/api/search` and the frontend static files | `404 Not Found` |
| `OPTIONS /api/search` | `204 No Content` (CORS preflight) |
| Non-GET/OPTIONS methods | `405 Method Not Allowed` |

## Quick checks

```bash
curl -i 'http://127.0.0.1:8080/'
curl -s 'http://127.0.0.1:8080/api/search?q=apple&limit=5' | python3 -m json.tool
curl -i 'http://127.0.0.1:8080/api/search?q=apple&page=1&limit=10'
curl -i 'http://127.0.0.1:8080/api/search?q=apple&page=2&limit=10'
curl -s 'http://127.0.0.1:8080/api/search?q=%22distributed+search%22'
curl -s 'http://127.0.0.1:8080/api/search?q='
```

## Notes

- Only `GET` and `OPTIONS` are supported.
- `limit` is clamped to `10`.
- `page` defaults to `1`.
- Result scores are real floating-point values from the ranker.
- `/`, `/results.html`, and frontend assets are served from `frontend/`.
- A blank query returns an empty result set.
- The coordinator returns partial results when a worker times out or is unreachable.
