# Index File Format (`.sfi`)

This document describes the on-disk format currently written by `src/index.cpp`.

## Top-Level Layout

1. Dictionary header:
   - `u64 document_count`
   - `u64 unique_words_plus_one`
2. Dictionary hash table:
   - `table_slots = unique_words_plus_one * 2`
   - each slot stores:
     - `u64 string_offset`
     - `u64 posting_offset`
3. String buffer:
   - NUL-terminated term strings
4. Posting-list region:
   - concatenated posting lists; dictionary `posting_offset` values are relative to this region start

## Posting List Layout

Each posting list uses:

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

The term `DOCUMENT_END` is a synthetic sentinel written at the end of every document. Its posting list has the same header structure as any other term, but appends a metadata section after the position delta stream:

- For each document (in posting order):
  - `url`: varint-encoded byte length, then raw bytes (no NUL)
  - `title`: varint-encoded byte length, then raw bytes (no NUL)
  - `ranking_input`: `u64` (little-endian, `RankingInput`)

The position deltas for `DOCUMENT_END` encode per-document token counts.

## Known Limitation

`string_offset == 0` is used as the empty-slot sentinel in the dictionary hash table. This collides with a valid first term at offset 0 in the string buffer, making that slot ambiguous. A TODO exists in the code to either choose a different sentinel or ensure offset 0 is never valid.
