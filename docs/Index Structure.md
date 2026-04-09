VLQs use the same encoding as [protobuf varints](https://protobuf.dev/programming-guides/encoding/#varints).

## Current On-Disk Format (`index.sfi`)

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
│       ├── u64:   positions_count
│       ├── u64:   positions_count  (duplicate — see Known Issues)
│       └── u64:   postings_offset  (byte offset into Postings Body)
└── Postings Body  (starts at headerSize() bytes into the file)
    └── concatenated per-term posting blobs
```

The header is written sequentially using `avengine::Writer` and flushed to
offset 0 **after** the postings body has been streamed to disk.
The dictionary is no longer a hash table — terms appear in map-iteration order
and lookup requires a linear scan of the term table.

## Current Posting Blob Layout (Per Term)

```txt
Posting Blob
├── u64: document_frequency
├── u64: collection_frequency
├── u64: skip_count
├── Skip section (skip_count entries, each varint-encoded):
│   ├── varint: position_index
│   ├── varint: position_value (absolute)
│   └── varint: delta_stream_byte_offset
├── Position section:
│   └── varint-encoded delta positions
└── Metadata section (DOCUMENT_END term only):
    └── per-document entries in posting order:
        ├── varint + bytes: url (length-prefixed)
        ├── varint + bytes: title (length-prefixed)
        └── u64: ranking_input (RankingInput)
```

## Field Decorators

Terms are stored with a single-character prefix indicating their source field:

| Prefix | Field  | Example |
|--------|--------|---------|
| (none) | body   | `cat`   |
| `#`    | title  | `#cat`  |
| `/`    | URL    | `/cat`  |

The same word in different fields produces separate posting lists. Decoration is
applied at index-build time — `HtmlParser` separates `words` (body) and
`titleWords` (title); the crawler passes the document URL as the URL phrase.
Ranking can boost `#`/`/` prefixed matches independently.

## Notes

- All multi-byte integers are little-endian.
- No sentinel is written at the end of a posting blob.
- Blob boundaries are inferred from `postings_offset` values in the term table.
- Skip pointers are sampled at every `floor(sqrt(collection_frequency))` positions;
  not emitted when stride <= 1.
- The special `DOCUMENT_END` term stores one posting per document; its position
  deltas encode per-document token counts, and document metadata is appended
  inline after the position delta stream.

## Known Issues

- **Duplicate `positions_count`**: each term record writes `pl.positions.size()`
  twice; one should be `pl.postings.size()` (document frequency).
- **Dead declaration**: `indexer.h` still declares `buildStringTable(Writer&)`
  with no definition. It was replaced by `headerSize()` and inline writes.
- **Unused writer**: `serialize()` declares `avengine::Writer body` that is
  never used.

## Tools

- `src/indexer/indexer.cpp` builds `index.sfi` via `Index::serialize()`.
- `headerSize()` computes the exact header byte size as:
  `5 * 8 + sum over terms of (4 * 8 + len(word))`
