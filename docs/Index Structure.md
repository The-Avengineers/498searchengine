VLQs use the same encoding as [protobuf varints](https://protobuf.dev/programming-guides/encoding/#varints).

## Current On-Disk Format (`index.sfi`)

```txt
Index File
├── Header (16 bytes total)
│   ├── u64: document_count
│   └── u64: unique_words_plus_one
├── Dictionary Table (flat hash table)
│   └── table_slots = unique_words_plus_one * 2
│       └── each slot (16 bytes):
│           ├── u64: string_offset (into String Buffer)
│           └── u64: posting_offset (into Posting Lists region)
├── String Buffer
│   └── concatenated null-terminated terms
└── Posting Lists region
    └── concatenated per-term posting blobs
```

The dictionary uses open addressing with linear probing

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

The same word in different fields produces separate posting lists. Decoration is applied at index-build time — `HtmlParser` separates `words` (body) and `titleWords` (title); the crawler passes the document URL as the URL phrase. Ranking can boost `#`/`/` prefixed matches independently.

## Notes
- No sentinel is written at the end of a posting blob.
- Blob boundaries are inferred from the next-greater `posting_offset`.
- Skip pointers are sampled at every `floor(sqrt(collection_frequency))` positions; not emitted when stride <= 1.
- The special `DOCUMENT_END` term stores one posting per document; its position deltas encode per-document token counts, and document metadata is appended inline after the position delta stream.

## Known Limitation

`string_offset == 0` is currently treated as an "empty dictionary slot" sentinel in write/read paths.
That collides with a valid first term at offset 0, so the first serialized term may be ambiguous.

## Tools

- `src/index_builder.cpp` builds `index.sfi`.
- `src/index_reader.cpp` reads this format in a best-effort way.

## Planned / TODO Format Fields

The code contains TODOs for future additions:
- magic/version bytes in file header
- richer posting/metastream structure
