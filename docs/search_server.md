# search_server

`search_server` is the HTTP entrypoint for the online search path. It loads `.sfi` index shards
from a directory, handles `GET /api/search` requests, returns ranked results as JSON, and serves
the checked-in frontend from `frontend/`.

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

Then open:

```text
http://127.0.0.1:8080/
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
| `/`, `/results.html`, and frontend assets | `200 OK` when file exists |
| Anything outside `/api/search` and the frontend static files | `404 Not Found` |
| `OPTIONS /api/search` | `204 No Content` (CORS preflight) |
| Non-GET/OPTIONS methods | `405 Method Not Allowed` |

## Quick checks

```bash
curl -i 'http://127.0.0.1:8080/'
curl -i 'http://127.0.0.1:8080/api/search?q=apple&page=1&limit=10'
curl -i 'http://127.0.0.1:8080/api/search?q=apple&page=2&limit=10'
curl -s 'http://127.0.0.1:8080/api/search?q=%22distributed+search%22'
curl -s 'http://127.0.0.1:8080/api/search?q='
```

Notes:

- only `GET` and `OPTIONS` are supported
- `limit` is clamped to `10`
- `page` defaults to `1`
- result scores are real floating-point values from the ranker
- `/`, `/results.html`, and frontend assets are served from `frontend/`
