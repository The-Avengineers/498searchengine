VLQs use same encoding as [protobuf](https://protobuf.dev/programming-guides/encoding/#varints).

```txt
Inverted Index
├── Dictionary
│   ├── Number of documents
│   ├── Number of posting lists
│   └── Hash Table using quadratic probing
│       ├── Word/Term offset into string buffer
│       └── Offset into posting lists
├── String Buffer
└── Posting Lists
    ├── Posting List
    ├── Posting List
    ├── ...
    └── Sentinel
```

```txt
Posting List:
├── Common Header
│   ├── Document frequency
│   ├── Collection frequency
│   ├── Posting list length
│   └── Metastream type identifier
├── Type-Specific Data (varies by metastream)
│   └── [For End-of-Doc]: PageRank, doc length, URL, static rank
├── Index (acceleration structure)
│   ├── Synchronization points (every N bytes)
│   └── Skip pointers (every M postings)
└── Posts (variable number)
    ├── Positions (delta encoded with VLQ)
    └── Sentinel (delta of '\0')
```
