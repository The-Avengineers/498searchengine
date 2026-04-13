#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import pathlib


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--crawler-json")
    parser.add_argument("--search-json", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--title", default="Profiling Summary",
                        help="Top-level heading for the summary document")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    search = json.loads(pathlib.Path(args.search_json).read_text(encoding="utf-8"))
    crawler = None
    if args.crawler_json:
      crawler = json.loads(pathlib.Path(args.crawler_json).read_text(encoding="utf-8"))

    top_hit_samples = [sample for sample in search["samples"] if sample.get("group", "top") != "coverage"]
    coverage_samples = [sample for sample in search["samples"] if sample.get("group", "top") == "coverage"]

    top_samples = sorted(
        top_hit_samples if top_hit_samples else search["samples"],
        key=lambda sample: (
            sample.get("mean_total_hits", sample.get("total_hits", 0)),
            -sample["mean_client_ms"],
        ),
        reverse=True,
    )[:15]

    lines = [
        f"## {args.title}",
        "",
        "### Search HTTP",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Queries profiled | {search['query_count']} |",
        f"| Iterations per query | {search['iterations_per_query']} |",
        f"| Shards loaded | {search.get('shard_count', 0)} |",
        f"| Queries with hits | {search.get('nonzero_hit_query_count', 0)} |",
        f"| Mean client ms | {search['mean_client_ms']:.2f} |",
        f"| P50 client ms | {search['p50_client_ms']:.2f} |",
        f"| P95 client ms | {search['p95_client_ms']:.2f} |",
        "",
        "| Input | Value |",
        "| --- | --- |",
        f"| Query file | `{search['queries_file']}` |",
        f"| Index dir | `{search['index_dir']}` |",
        "",
        "| Query | Hits | Mean client ms | P50 ms | P95 ms | Server ms | Parse ms | Solve ms | Collect ms | Rank ms |",
        "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
    ]

    if crawler is not None:
        lines[2:2] = [
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
        ]

    for sample in top_samples:
        hits = int(round(sample.get('mean_total_hits', sample.get('total_hits', 0))))
        lines.append(
            f"| `{sample['query']}` | {hits} |"
            f" {sample['mean_client_ms']:.2f} |"
            f" {sample['p50_client_ms']:.2f} |"
            f" {sample['p95_client_ms']:.2f} |"
            f" {sample['mean_server_took_ms']:.2f} |"
            f" {sample.get('mean_parse_ms', 0):.2f} |"
            f" {sample.get('mean_solve_ms', 0):.2f} |"
            f" {sample.get('mean_collect_ms', 0):.2f} |"
            f" {sample.get('mean_rank_ms', 0):.2f} |"
        )

    if coverage_samples:
        lines.extend(
            [
                "",
                "### Query Shape Coverage",
                "",
                "| Label | Query | Hits | Mean client ms | P50 ms | P95 ms | Server ms | Parse ms | Solve ms | Collect ms | Rank ms |",
                "| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
            ]
        )

        zero_hit_coverage: list[str] = []
        for sample in coverage_samples:
            hits = int(round(sample.get('mean_total_hits', sample.get('total_hits', 0))))
            if hits <= 0:
                zero_hit_coverage.append(sample["label"])
            lines.append(
                f"| `{sample['label']}` | `{sample['query']}` |"
                f" {hits} |"
                f" {sample['mean_client_ms']:.2f} |"
                f" {sample['p50_client_ms']:.2f} |"
                f" {sample['p95_client_ms']:.2f} |"
                f" {sample['mean_server_took_ms']:.2f} |"
                f" {sample.get('mean_parse_ms', 0):.2f} |"
                f" {sample.get('mean_solve_ms', 0):.2f} |"
                f" {sample.get('mean_collect_ms', 0):.2f} |"
                f" {sample.get('mean_rank_ms', 0):.2f} |"
            )

        if zero_hit_coverage:
            lines.extend(
                [
                    "",
                    "> Warning: coverage queries with zero hits: "
                    + ", ".join(f"`{label}`" for label in zero_hit_coverage)
                    + ".",
                ]
            )

    if len(top_hit_samples if top_hit_samples else search["samples"]) > len(top_samples):
        lines.extend(
            [
                "",
                f"_Showing top {len(top_samples)} non-coverage queries by hit count out of {len(search['samples'])} profiled queries._",
            ]
        )

    if search.get("nonzero_hit_query_count", 0) == 0:
        lines.extend(
            [
                "",
                "> Warning: shards were loaded, but every profiled query returned zero hits.",
            ]
        )

    pathlib.Path(args.output).write_text("\n".join(lines) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
