# search_server

`search_server` is the HTTP entrypoint for the online search path. It loads `.sfi` index shards
from a directory, handles `GET /api/search` requests, and returns ranked results as JSON.

## Build and run

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target search_server -j 4
./build/search_server <port> <index-dir>
```

| Argument | Default | Description |
|----------|---------|-------------|
| `port` | `8080` | TCP port to listen on |
| `index-dir` | `.` | Directory containing `.sfi` shard files |

Example:

```bash
./build/search_server 8080 /data/index
```

The server prints a startup line and then handles one request at a time:

```
search_server listening on port 8080 using index dir /data/index
```

## API

### `GET /api/search`

Query parameters:

| Parameter | Required | Description |
|-----------|----------|-------------|
| `q` | yes | Search query (URL-encoded) |
| `limit` | no | Max results to return (clamped to 1–100, default 10) |

Response:

```json
{
  "query": "distributed search",
  "limit": 10,
  "total_hits": 3,
  "took_ms": 2,
  "parse_ms": 0,
  "solve_ms": 1,
  "collect_ms": 1,
  "rank_ms": 0,
  "errors": [],
  "results": [
    { "url": "https://example.com/page", "title": "Distributed Search Systems", "score": 1 },
    ...
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

### Query syntax

| Syntax | Example | Meaning |
|--------|---------|---------|
| Single term | `apple` | Documents containing "apple" |
| AND | `apple banana` | Documents containing both terms |
| Phrase | `"apple pie"` | Documents containing the exact phrase |
| OR | `apple OR banana` | Documents containing either term |

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
| Anything outside `/api/search` | `404 Not Found` |
| `OPTIONS /api/search` | `204 No Content` (CORS preflight) |
| Non-GET/OPTIONS methods | `405 Method Not Allowed` |

## Quick checks

```bash
# Basic query
curl -s 'http://127.0.0.1:8080/api/search?q=apple&limit=5' | python3 -m json.tool

# Phrase query
curl -s 'http://127.0.0.1:8080/api/search?q=%22distributed+search%22'

# Empty query (returns empty results, no error)
curl -s 'http://127.0.0.1:8080/api/search?q='
```
