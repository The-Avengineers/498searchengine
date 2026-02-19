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
├── u64: collection_frequency_placeholder (currently 0xdeadbeef)
└── varint stream:
    └── delta-encoded token positions (global index offsets)
```

Notes:
- There is currently no per-document posting structure in the serialized data.
- No sentinel is written at the end of a posting blob.
- Blob boundaries are inferred from the next-greater `posting_offset`.
- Acceleration structures (sync points / skip pointers) are not yet serialized.

## Known Limitation

`string_offset == 0` is currently treated as an "empty dictionary slot" sentinel in write/read paths.
That collides with a valid first term at offset 0, so the first serialized term may be ambiguous.

## Tools

- `avengine/src/index_builder.cpp` builds `index.sfi`.
- `avengine/src/index_reader.cpp` reads this format in a best-effort way.

## Planned / TODO Format Fields

The code contains TODOs for future additions:
- magic/version bytes in file header
- true collection frequency
- skip pointers / synchronization points
- richer posting/metastream structure
