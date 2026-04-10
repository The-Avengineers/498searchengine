#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import pathlib


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cpu-json", required=True)
    parser.add_argument("--output", required=True)
    return parser.parse_args()


def escape_cell(text: str) -> str:
    return text.replace("|", "\\|")


def main() -> int:
    args = parse_args()
    profile = json.loads(pathlib.Path(args.cpu_json).read_text(encoding="utf-8"))

    lines = [
        "### Search CPU Callgraph",
        "",
        "> Profiled with `gprof` via `search_benchmark`, so this excludes HTTP/network overhead and focuses on in-process search CPU time.",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Profiler | `{profile['profiler']}` |",
        f"| Queries profiled | {profile['query_count']} |",
        f"| Iterations per query | {profile['iterations_per_query']} |",
        f"| Warmup iterations | {profile['warmup_iterations']} |",
        f"| Shards loaded | {profile.get('shard_count', 0)} |",
        f"| Query file | `{profile['queries_file']}` |",
        f"| Raw callgraph | `{profile['gprof_report_path']}` |",
        "",
        "| Top Self Time Function | Self % | Self s | Calls |",
        "| --- | ---: | ---: | ---: |",
    ]

    for row in profile.get("top_self_functions", [])[:10]:
        lines.append(
            f"| `{escape_cell(row['name'])}` | {row['percent_time']:.2f} |"
            f" {row['self_seconds']:.2f} | {row.get('calls', '') or ''} |"
        )

    lines.extend(
        [
            "",
            "| Top Total Time Function | Total % | Self s | Children s | Calls |",
            "| --- | ---: | ---: | ---: | ---: |",
        ]
    )

    for row in profile.get("top_total_functions", [])[:10]:
        lines.append(
            f"| `{escape_cell(row['name'])}` | {row['percent_time']:.2f} |"
            f" {row['self_seconds']:.2f} | {row['children_seconds']:.2f} |"
            f" {row.get('calls', '') or ''} |"
        )

    pathlib.Path(args.output).write_text("\n".join(lines) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
