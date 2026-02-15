VLQs use the same encoding as [protobuf varints](https://protobuf.dev/programming-guides/encoding/#varints).

## Current On-Disk Format (`index.sfi`) - v2

```txt
Index File
├── Header (40 bytes total)
│   ├── u64: magic
│   ├── u64: version
│   ├── u64: document_count
│   ├── u64: unique_words_plus_one
│   └── u64: field_count (3)
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

Dictionary probing is linear. Empty slots in v2 use the sentinel pair:

- `string_offset == UINT64_MAX`
- `posting_offset == UINT64_MAX`

## Current Posting Blob Layout (Per Term, v2)

```txt
Posting Blob Header (56 bytes)
├── u64: document_frequency
├── u64: normal_collection_frequency
├── u64: title_collection_frequency
├── u64: anchor_collection_frequency
├── u64: normal_skip_count
├── u64: title_skip_count
└── u64: anchor_skip_count

Then field streams in this order:
1) normal: [skip entries][delta positions]
2) title:  [skip entries][delta positions]
3) anchor: [skip entries][delta positions]
```

Each skip entry is three varints:

- `position_index`
- `absolute_position`
- `delta_stream_byte_offset`

## Anchor Text Indexing Behavior

- Anchor text is its own field (not merged into body/title).
- Anchor tokens are indexed against the target URL's document ID.
- Links whose normalized target URL is not in the corpus are ignored.
- For each source->target edge, normalized anchor tokens are deduplicated.

## Backward Compatibility

- `avengine/src/index_reader.cpp` supports both v1 and v2.
- New writes from `avengine/src/index.cpp` produce v2.

## Legacy v1 Summary

```txt
Header (16 bytes):
- u64 document_count
- u64 unique_words_plus_one

Dictionary slots:
- u64 string_offset
- u64 posting_offset

Posting header (24 bytes):
- u64 document_frequency
- u64 collection_frequency
- u64 skip_count
```

## Tools

- `avengine/src/index_builder.cpp` builds `index.sfi`.
- `avengine/src/index_reader.cpp` reads v1 and v2 formats.
