# Index File Format (`.sfi`)

This document describes the on-disk format written by `src/indexer/indexer.cpp`.

## Top-Level Layout

The file is split into two regions written in a two-pass style:

1. **Header**: written last (seeked back to offset 0 after the postings are flushed).
2. **Postings body**: written first, starting at `headerSize()` bytes into the file.

```txt
Index File
├── Header
│   ├── u64: magic  (0x008FB8EF84979FF0)
│   ├── u64: document_count
│   ├── u64: total_unique_words
│   ├── u64: token_offset  (total token positions ingested)
│   ├── u64: sizeof(DocumentMetadata::input)
│   └── Term Table (one record per unique word, in map-iteration order):
│       ├── u64:   word_len
│       ├── bytes: word  (raw, not NUL-terminated; word_len bytes)
│       ├── u64:   positions_count  (number of token positions for this term)
│       ├── u64:   positions_count  (duplicate — see Known Issues below)
│       └── u64:   postings_offset  (byte offset into the Postings Body)
└── Postings Body  (starts at headerSize() bytes)
    └── concatenated per-term posting blobs
```

All multi-byte integers are **little-endian**.

## Posting List Layout

Each posting list blob uses:

1. Fixed header:
   - `u64 document_frequency`
   - `u64 collection_frequency`
   - `u64 skip_count`
2. Skip section (`skip_count` entries), each entry varint-encoded:
   - `position_index`
   - `absolute_position`
   - `delta_stream_byte_offset`
3. Position section:
   - varint-encoded delta positions

## Skip Pointer Policy

- Skip pointers are sampled every `floor(sqrt(collection_frequency))` positions.
- No skip pointers are emitted when the stride is <= 1.
- `delta_stream_byte_offset` points into the start of the position delta stream for fast seek/jump.

## DOCUMENT_END Special Posting List

The term `DOCUMENT_END` is a synthetic sentinel written at the end of every
document. Its posting list has the same structure as any other term, but appends
a metadata section after the position delta stream:

- For each document (in posting order):
  - `url`: varint-encoded byte length, then raw bytes (no NUL)
  - `title`: varint-encoded byte length, then raw bytes (no NUL)
  - `ranking_input`: `u64` (little-endian, `RankingInput`)

The position deltas for `DOCUMENT_END` encode per-document token counts.

## Field Decorators

Terms are stored with a single-character prefix to indicate which field of the
document they came from:

| Prefix | Field  | Example      |
|--------|--------|--------------|
| (none) | body   | `cat`        |
| `#`    | title  | `#cat`       |
| `/`    | URL    | `/cat`       |

The same word appearing in multiple fields produces separate posting lists
(e.g., `"cat"`, `"#cat"`, and `"/cat"` are three independent entries).
Decoration is applied at index-build time in `indexDocument()`.
