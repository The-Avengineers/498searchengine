# avengine

Hybrid CLI for the AVENGINE search server.

## Install

```bash
npm install
npm run build
```

For local development inside this repo:

```bash
npm run cli -- --help
```

To expose the `avengine` command globally from this checkout:

```bash
npm link
avengine --help
```

## Commands

```bash
avengine
avengine query "apple"
avengine query "apple" --verbose
avengine lucky "apple"
avengine crawler status
avengine crawler index
```

## Config

The CLI reads these environment variables:

- `AVENGINE_SERVER_URL` default `http://127.0.0.1:8080`
- `AVENGINE_RUNTIME_BIN_ROOT` default `/avengine/bin`
- `AVENGINE_DATA_ROOT` default `/data`
- `AVENGINE_CONFIG_ROOT` default `/config`

## Notes

- `avengine` without a subcommand launches the interactive TUI.
- Query results use terminal hyperlinks when the terminal supports them.
- Crawler commands target the runtime-box helpers documented in `docs/crawler_usage.md`.
- Offline game is intentionally left as “coming soon”.
