# search_server

## What it is right now

`search_server` is the tiny HTTP entrypoint for the online search path.

Current scope:

- `GET /api/search?q=...&limit=...`
- JSON only
- placeholder transport response
- no static file serving yet
- no solver or ranker wiring yet

## Build and run

```bash
cmake -S . -B build-test -DCMAKE_BUILD_TYPE=Debug
cmake --build build-test --target search_server -j 6
./build-test/search_server 8080
```

If you do not pass a port, it defaults to `8080`.

## API shape

Success:

```http
GET /api/search?q=apple&limit=10
```

```json
{
  "query": "apple",
  "limit": 10,
  "total_hits": 0,
  "took_ms": 0,
  "results": [],
  "phase": "transport-only"
}
```

Missing or blank query:

```json
{
  "query": "",
  "errors": [
    { "message": "missing query" }
  ],
  "results": []
}
```

## Quick checks

```bash
curl -i 'http://127.0.0.1:8080/api/search?q=apple&limit=10'
curl -i 'http://127.0.0.1:8080/api/search?q=++'
```

Notes:

- only `GET` is supported
- `limit` is clamped to `10`
- anything outside `/api/search` returns `404` in this batch
