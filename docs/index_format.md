# Index File Format (`.sfi`)

This document describes the current writer behavior in `src/index.cpp`.

## Decorated Terms

The writer now distinguishes fields by decorating the term text itself:

- body terms: `term`
- title terms: `#term`
- URL terms: `/term`

Anchor text is not indexed.

## Version Detection

- **v4**: starts with `[u64 magic][u64 version]` where:
  - `magic = 0x3256465345584449`
  - `version = 4`

## Top-Level Layout

1. Header (4 x `u64`):
   - `magic`
   - `version`
   - `document_count`
   - `unique_words_plus_one`
2. Dictionary hash table:
   - `table_slots = unique_words_plus_one * 2`
   - each slot:
     - `u64 string_offset`
     - `u64 posting_offset`
   - empty slot sentinel is `UINT64_MAX` for both fields
3. String buffer:
   - concatenated NUL-terminated terms, including decorated terms such as `#term` and `/term`
4. Posting-list region:
   - concatenated posting blobs
   - `posting_offset` values are relative to this region start

## Posting Blob Layout (per term)

Header (2 x `u64`):

1. `document_frequency`
2. `collection_frequency`

Then one populated position stream:

1. delta-encoded positions for that exact decorated term
