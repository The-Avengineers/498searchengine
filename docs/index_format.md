# Index File Format (`.sfi`)

This document describes both index formats supported by `src/index_reader.cpp`.

## Version Detection

- **v2**: starts with `[u64 magic][u64 version]` where:
  - `magic = 0x3256465345584449`
  - `version = 2`
- **v1**: legacy format with no magic/version header.

## Top-Level Layout (v2)

1. Header (5 x `u64`):
   - `magic`
   - `version`
   - `document_count`
   - `unique_words_plus_one`
   - `field_count` (currently `3`)
2. Dictionary hash table:
   - `table_slots = unique_words_plus_one * 2`
   - each slot:
     - `u64 string_offset`
     - `u64 posting_offset`
   - empty slot sentinel is `UINT64_MAX` for both fields
3. String buffer:
   - NUL-terminated term strings
4. Posting-list region:
   - concatenated posting blobs
   - `posting_offset` values are relative to this region start

## Posting Blob Layout (v2, per term)

Header (7 x `u64`):

1. `document_frequency`
2. `normal_collection_frequency`
3. `title_collection_frequency`
4. `anchor_collection_frequency`
5. `normal_skip_count`
6. `title_skip_count`
7. `anchor_skip_count`

Then three field streams in order:

1. normal stream
2. title stream
3. anchor stream

Each field stream contains:

1. skip section (`skip_count` entries), each entry varint triplet:
   - `position_index`
   - `absolute_position`
   - `delta_stream_byte_offset`
2. position section:
   - varint-encoded delta positions

## Skip Pointer Policy

- Skip pointers are sampled every `floor(sqrt(collection_frequency))` positions.
- No skip pointers are emitted when the stride is `<= 1`.

## Anchor Indexing Semantics

- Anchor text is indexed as a separate field (`ANCHOR`).
- Anchor tokens are indexed against the **target document**.
- Only links whose normalized target URL maps to an in-corpus document are indexed.
- Per source->target edge dedup is applied: each normalized anchor token is indexed at most once per edge.

## Legacy v1 Summary

v1 layout remains readable by `index_reader`:

1. Header (`u64 document_count`, `u64 unique_words_plus_one`)
2. Dictionary table (`u64 string_offset`, `u64 posting_offset` per slot)
3. String buffer
4. Posting blobs with header:
   - `u64 document_frequency`
   - `u64 collection_frequency`
   - `u64 skip_count`
   - followed by skip entries + a single delta stream
