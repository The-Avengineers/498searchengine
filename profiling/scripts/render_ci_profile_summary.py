#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import pathlib


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--crawler-json", required=True)
    parser.add_argument("--search-json", required=True)
    parser.add_argument("--output", required=True)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    crawler = json.loads(pathlib.Path(args.crawler_json).read_text(encoding="utf-8"))
    search = json.loads(pathlib.Path(args.search_json).read_text(encoding="utf-8"))

    lines = [
        "## Profiling Summary",
        "",
        "### Crawler + Indexer",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Documents processed | {crawler['document_count']} |",
        f"| Total bytes | {crawler['total_bytes']} |",
        f"| Total words | {crawler['total_words']} |",
        f"| Normalized links | {crawler['normalized_link_count']} |",
        f"| Parse + index ms | {crawler['parse_index_ms']} |",
        f"| Serialize ms | {crawler['serialize_ms']} |",
        f"| Total ms | {crawler['total_ms']} |",
        f"| Docs/sec | {crawler['docs_per_sec']:.2f} |",
        f"| MB/sec | {crawler['mb_per_sec']:.2f} |",
        "",
        "### Search HTTP",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Queries profiled | {search['query_count']} |",
        f"| Iterations per query | {search['iterations_per_query']} |",
        f"| Mean client ms | {search['mean_client_ms']:.2f} |",
        f"| P50 client ms | {search['p50_client_ms']:.2f} |",
        f"| P95 client ms | {search['p95_client_ms']:.2f} |",
        "",
        "| Query | Hits | Mean client ms | P50 ms | P95 ms | Server ms | Parse ms | Solve ms | Collect ms | Rank ms |",
        "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
    ]

    for sample in search["samples"]:
        lines.append(
            f"| `{sample['query']}` | {sample.get('mean_total_hits', sample.get('total_hits', 0)):.2f} |"
            f" {sample['mean_client_ms']:.2f} |"
            f" {sample['p50_client_ms']:.2f} |"
            f" {sample['p95_client_ms']:.2f} |"
            f" {sample['mean_server_took_ms']:.2f} |"
            f" {sample.get('mean_parse_ms', 0):.2f} |"
            f" {sample.get('mean_solve_ms', 0):.2f} |"
            f" {sample.get('mean_collect_ms', 0):.2f} |"
            f" {sample.get('mean_rank_ms', 0):.2f} |"
        )

    pathlib.Path(args.output).write_text("\n".join(lines) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
