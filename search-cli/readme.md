# avengine CLI

A terminal UI for AVEngine — search queries, live crawler stats, system status, and remote deploy container access over SSH.

## Requirements

- Node.js 16+
- npm

## Install

```bash
cd search-cli
npm install
npm install -g .
```

This installs the `avengine` command globally. To uninstall later:

```bash
npm uninstall -g avengine
```

## Usage

```
avengine [command] [options]
```

Running with no arguments opens the interactive TUI.

## Commands

### Interactive mode

```bash
avengine
```

A vim-style terminal UI. Key bindings:

| Key | Action |
|-----|--------|
| `i` | Enter insert mode — type a query and press Enter to search |
| `:` | Enter command mode |
| `?` | Toggle help |
| `q` | Quit (from normal mode) |
| `Ctrl+C` | Force quit |

**Command mode** (type after pressing `:`):

| Command | Description |
|---------|-------------|
| `:query [text]` | Search in standard mode |
| `:verbose [text]` | Search with timing breakdown and score display |
| `:lucky [text]` | Open the top result in your browser |
| `:next` / `:prev` | Page through results |
| `:page N` | Jump to page N |
| `:stats` | Open live crawler stats dashboard |
| `:status` | One-shot system status snapshot |
| `:open` | Start the search server and open the web UI in your browser |
| `:crawler status\|start\|stop\|index\|watch` | Crawler control |
| `:help` | Show help |
| `:quit` | Quit |

---

### Search

```bash
avengine query "spider man"
avengine query "spider man" --verbose
avengine query "spider man" --page 2
```

`--verbose` shows per-phase query timing (parse / solve / collect / rank) with an ASCII bar chart.

### Lucky

```bash
avengine lucky "spider man"
```

Runs a search and opens the top result in your default browser.

### Open web UI

```bash
avengine open
avengine open --port 9090
```

Starts the search server (if not already running) and opens `http://localhost:8080` in your browser. When connected to a remote container via SSH this creates a tunnel so the browser connects through your local machine.

Use `--port` to choose a different local port if 8080 is already in use.

### Crawler

```bash
avengine crawler status      # per-worker state, shard counts, checkpoint
avengine crawler start       # start the crawler swarm
avengine crawler stop        # stop the crawler swarm
avengine crawler index       # show index/corpus stats
avengine crawler watch       # open live stats dashboard (same as avengine stats)
avengine crawler profile     # run the profiling pipeline and print results
```

### Live stats dashboard

```bash
avengine stats               # live-updating table, refreshes every 2s
avengine stats --once        # fetch once and exit
```

Shows per-worker state, documents crawled, docs/sec, SFI shard count, and checkpoint status, plus total corpus shards and search server reachability.

### System status

```bash
avengine status
```

One-shot snapshot: search server reachability and latency, crawler swarm summary, data usage, corpus shard count.

### Profile

```bash
avengine profile
```

Runs `avengine-profile.sh` and streams the output. This is a long-running benchmark (60+ seconds) — run it one-shot only.

### Help

```bash
avengine help
avengine --help
```

---

## Remote deploy container

Set `AVENGINE_SSH_HOST` to transparently run all commands against a remote container over SSH:

```bash
export AVENGINE_SSH_HOST=myserver.example.com
avengine stats               # crawler stats from remote container
avengine status              # system status from remote container
avengine open                # SSH tunnel → open browser locally
avengine query "apple"       # search against remote index
```

Or use `--host` to set it per-command:

```bash
avengine --host myserver.example.com stats
avengine --host myserver.example.com open
```

### SSH environment variables

| Variable | Default | Description |
|----------|---------|-------------|
| `AVENGINE_SSH_HOST` | — | Container hostname or IP |
| `AVENGINE_SSH_PORT` | `2222` | SSH port |
| `AVENGINE_SSH_USER` | `engine` | SSH username |
| `AVENGINE_SSH_KEY` | — | Path to SSH private key |

### How `avengine open` works remotely

1. Starts the search server on the container in daemon mode (idempotent)
2. Opens a local SSH tunnel: `localhost:8080 → container:8080`
3. Polls until the server is reachable through the tunnel
4. Opens your browser to `http://localhost:8080`
5. Keeps the tunnel alive — press `q` or `Ctrl+C` to close it and exit

---

## Options

| Flag | Description |
|------|-------------|
| `--server URL` | Override the search server URL (`AVENGINE_SERVER_URL`) |
| `--host HOST` | SSH host for remote container (`AVENGINE_SSH_HOST`) |
| `--page N` | Starting page number (default 1) |
| `--limit N` | Results per page (default 10, max 10) |
| `--verbose` | Verbose search output with timing bar |
| `--once` | For `avengine stats`: fetch once and exit |
| `--port N` | Local port for `avengine open` tunnel (default 8080) |

## Environment variables

| Variable | Default | Description |
|----------|---------|-------------|
| `AVENGINE_SERVER_URL` | `http://127.0.0.1:8080` | Search server URL (auto-set from SSH host if not provided) |
| `AVENGINE_RUNTIME_BIN_ROOT` | `/avengine/bin` | Path to avengine runtime scripts |
| `AVENGINE_DATA_ROOT` | `/data` | Path to data directory |
| `AVENGINE_CONFIG_ROOT` | `/config` | Path to config directory |
| `AVENGINE_SSH_HOST` | — | Remote container SSH host |
| `AVENGINE_SSH_PORT` | `2222` | Remote container SSH port |
| `AVENGINE_SSH_USER` | `engine` | Remote container SSH user |
| `AVENGINE_SSH_KEY` | — | SSH private key path |

## Development

```bash
cd search-cli
npm install
node_modules/.bin/tsc --watch   # rebuild on change
node dist/source/cli.js         # run without global install
```
