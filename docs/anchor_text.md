# Anchor Text Integration

Anchor text — the visible words inside `<a>` tags — is a strong relevance signal because it reflects how other pages describe a document rather than how the document describes itself. This doc covers how anchor text flows from the crawler through to the ranker.

---

## Pipeline Overview

```
Crawler  →  ac_log.tsv  →  anchor_processor  →  anchor_NNNNN.sfi  →  SearchService  →  Ranker
```

1. **Crawler** writes inbound anchor text for each URL to `ac_log.tsv`.
2. **`anchor_processor`** (offline tool) reads `ac_log.tsv`, looks up each target URL in the existing `.sfi` shards, tokenizes the anchor words, and emits `anchor_NNNNN.sfi` files.
3. **`SearchService`** loads `anchor_*.sfi` files separately from main shards, builds a per-query URL→positions map, and passes anchor term positions to the ranker alongside body and title positions.
4. **Ranker strategies** score anchor text with weights slightly below title.

---

## Step 1: Crawler (`ac_log.tsv`)

The crawler writes one line per outgoing link to `ac_log.tsv`:

```
<target_url>\t<word1> <word2> ...
```

Words are normalized by the HTML parser: lowercase (`c | 32`), accent-folded to ASCII, non-alpha characters stripped. Each `write()` call is atomic at the kernel inode level, making the file safe to write from multiple crawler threads without explicit locking.

---

## Step 2: `anchor_processor` (offline)

Run after a crawl completes, from the index directory:

```sh
./anchor_processor
```

**What it does:**

1. Memory-maps `ac_log.tsv` and builds a `url → [anchor words]` map using zero-copy `string_slice` views into the mapped buffer.
2. Walks every `NNNNN.sfi` shard, extracts each document's URL from the `DOCUMENT_END` posting, looks it up in the map, tokenizes the anchor words with `PhraseKind::BODY` (same pipeline as body text — lowercase alpha runs, no field prefix), and emits them into a new `Index`.
3. Flushes to a new `anchor_NNNNN.sfi` file whenever the index offset exceeds `OFFSET_THRESHOLD` (32 MiB), then continues into a fresh shard.

**Output:** one or more `anchor_NNNNN.sfi` files in the same directory as the main shards.

### Tokenization compatibility

Anchor terms are stored with no field prefix (identical to body terms). The query lexer also produces lowercase alpha-only tokens via `std::tolower()`, which for ASCII produces identical results to the crawler's `c | 32` lowercasing. Anchor ISRs can therefore be opened with the same `queryTerms` vector used for body lookup — no prefix needed.

---

## Step 3: `SearchService` — anchor shard separation and lookup

### Loading

`LoadShards()` detects `anchor_` filename prefix via `IsAnchorShard()` and routes files into a separate `anchorShards_` list. Anchor shards are **not** fed into the constraint solver, so they never produce independent top-level matches or duplicate results.

### Per-query anchor map

Before the collect phase, `BuildAnchorPositionMap()` builds an `unordered_map<MatchPositions>` keyed by URL:

- For each anchor shard × each query term: open a `ISRWord`, walk all positions.
- On each document transition, look up the URL in the map. If the URL is new, insert a fresh `MatchPositions` (one `vector<size_t>` per query term).
- Accumulate term positions into the entry.

This map is built once per query and shared read-only across collect-phase worker threads.

### Collect phase

`CollectAllMatchPositions()` fills `context.anchorPositions[matchIdx]` by looking up each matched document's URL in the anchor map. If the URL appears in anchor shards, its term positions are copied into `anchorPositions`.

---

## Step 4: Ranker

`RunRankPhase()` sets `signals.anchor_term_positions` from `context.anchorPositions[matchIndex]` and passes it to every strategy via `RankSignals`. Four strategies incorporate anchor scoring:

| Strategy | Body | Title | Anchor |
|---|---|---|---|
| `ExactPhraseMatch` | 1.0 | ×0.9 | ×0.8 |
| `CorrectOrdering` | 1.0 | ×0.8 | ×0.7 |
| `EarlyQueryTermMatch` | 1.0 | ×0.7 | ×0.6 |
| `TermCoverage` | 1.0 | ×0.6 | ×0.5 |

Anchor weights are set slightly below title weights on the assumption that anchor text is less authoritative than the document's own title but more authoritative than arbitrary body text.

---

## Regenerating anchor shards

After any re-crawl or index rebuild, regenerate anchor shards:

```sh
cd /path/to/index/directory
./anchor_processor
```

Old `anchor_NNNNN.sfi` files should be deleted first if you want a clean rebuild:

```sh
rm -f anchor_*.sfi
./anchor_processor
```
