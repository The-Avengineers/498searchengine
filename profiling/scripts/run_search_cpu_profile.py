#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import pathlib
import re
import shutil
import subprocess
import tempfile

from query_fixture import load_search_queries, write_plain_query_list


FLAT_PROFILE_ROW = re.compile(
    r"^\s*(?P<percent>[\d.]+)\s+"
    r"(?P<cumulative>[\d.]+)\s+"
    r"(?P<self_seconds>[\d.]+)"
    r"(?:\s+(?P<calls>\S+)\s+(?P<self_per_call>\S+)\s+(?P<total_per_call>\S+))?"
    r"\s+(?P<name>.+?)\s*$"
)

CALL_GRAPH_ROW = re.compile(
    r"^\[(?P<index>\d+)\]\s+"
    r"(?P<percent>[\d.]+)\s+"
    r"(?P<self_seconds>[\d.]+)\s+"
    r"(?P<children_seconds>[\d.]+)\s+"
    r"(?P<rest>.+?)\s*$"
)

NOISE_SYMBOLS = {"_init", "main", "frame_dummy"}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--search-benchmark", required=True)
    parser.add_argument("--index-dir", required=True)
    parser.add_argument("--queries-file", required=True)
    parser.add_argument("--output-json", required=True)
    parser.add_argument("--output-report", required=True)
    parser.add_argument("--output-benchmark", required=True)
    parser.add_argument("--output-query-list", required=True)
    parser.add_argument("--iterations", type=int, default=100)
    parser.add_argument("--warmup", type=int, default=5)
    return parser.parse_args()


def shard_count(index_dir: pathlib.Path) -> int:
    return len(list(index_dir.glob("*.sfi")))


def normalize_calls(value: str | None) -> str:
    if not value:
        return ""
    return value


def parse_flat_profile(report: str) -> list[dict]:
    rows: list[dict] = []
    in_flat_profile = False
    seen_header = False
    for raw_line in report.splitlines():
        line = raw_line.rstrip("\n")
        if line.startswith("Flat profile:"):
            in_flat_profile = True
            seen_header = False
            continue
        if not in_flat_profile:
            continue
        if line.startswith("Call graph"):
            break
        if not seen_header:
            if line.lstrip().startswith("time") and "name" in line:
                seen_header = True
            continue
        if not line.strip():
            continue
        match = FLAT_PROFILE_ROW.match(line)
        if not match:
            continue
        name = match.group("name").strip()
        if name == "name":
            continue
        rows.append(
            {
                "name": name,
                "percent_time": float(match.group("percent")),
                "cumulative_seconds": float(match.group("cumulative")),
                "self_seconds": float(match.group("self_seconds")),
                "calls": normalize_calls(match.group("calls")),
            }
        )
    return rows


def parse_call_graph(report: str) -> list[dict]:
    rows: list[dict] = []
    in_call_graph = False
    seen_header = False
    for raw_line in report.splitlines():
        line = raw_line.rstrip("\n")
        if line.startswith("\t\t     Call graph") or line.startswith("             Call graph"):
            in_call_graph = True
            seen_header = False
            continue
        if not in_call_graph:
            continue
        if line.startswith(" This table describes the call tree"):
            break
        if not seen_header:
            if line.startswith("index % time"):
                seen_header = True
            continue
        if not line.startswith("["):
            continue
        match = CALL_GRAPH_ROW.match(line)
        if not match:
            continue
        rest = match.group("rest").strip()
        rest = re.sub(r"\s+\[\d+\]\s*$", "", rest)
        calls = ""
        name = rest
        parts = rest.split(None, 1)
        if len(parts) == 2 and re.fullmatch(r"[0-9+/]+", parts[0]):
            calls = parts[0]
            name = parts[1]
        rows.append(
            {
                "index": int(match.group("index")),
                "name": name,
                "percent_time": float(match.group("percent")),
                "self_seconds": float(match.group("self_seconds")),
                "children_seconds": float(match.group("children_seconds")),
                "total_seconds": float(match.group("self_seconds"))
                + float(match.group("children_seconds")),
                "calls": normalize_calls(calls),
            }
        )
    return rows


def is_noise_symbol(name: str) -> bool:
    if name in NOISE_SYMBOLS:
        return True
    if name.startswith("__") and "std::" not in name and "avengine::" not in name:
        return True
    return False


def main() -> int:
    args = parse_args()
    repo_root = pathlib.Path(__file__).resolve().parents[2]
    benchmark_path = pathlib.Path(args.search_benchmark).resolve()
    index_dir = pathlib.Path(args.index_dir).resolve()
    queries_file = pathlib.Path(args.queries_file).resolve()
    output_json = pathlib.Path(args.output_json).resolve()
    output_report = pathlib.Path(args.output_report).resolve()
    output_benchmark = pathlib.Path(args.output_benchmark).resolve()
    output_query_list = pathlib.Path(args.output_query_list).resolve()

    if shutil.which("gprof") is None:
        raise RuntimeError("gprof was not found in PATH")
    if not benchmark_path.exists():
        raise RuntimeError(f"search_benchmark was not found: {benchmark_path}")
    if shard_count(index_dir) == 0:
        raise RuntimeError(f"no .sfi shards found in {index_dir}")

    queries = load_search_queries(queries_file)
    write_plain_query_list(queries, output_query_list)

    output_json.parent.mkdir(parents=True, exist_ok=True)
    output_report.parent.mkdir(parents=True, exist_ok=True)
    output_benchmark.parent.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix="avengine_search_cpu_profile_") as tmpdir:
        tempdir = pathlib.Path(tmpdir)
        benchmark_query_list = tempdir / "queries.txt"
        write_plain_query_list(queries, benchmark_query_list)

        benchmark_result = subprocess.run(
            [
                str(benchmark_path),
                "--index-dir",
                str(index_dir),
                "--queries",
                str(benchmark_query_list),
                "--iterations",
                str(args.iterations),
                "--warmup",
                str(args.warmup),
            ],
            cwd=tempdir,
            check=True,
            capture_output=True,
            text=True,
        )
        output_benchmark.write_text(benchmark_result.stdout, encoding="utf-8")

        gmon_path = tempdir / "gmon.out"
        if not gmon_path.exists():
            raise RuntimeError(f"search_benchmark did not produce {gmon_path}")

        gprof_result = subprocess.run(
            ["gprof", str(benchmark_path), str(gmon_path), "--flat-profile", "--graph"],
            cwd=repo_root,
            check=True,
            capture_output=True,
            text=True,
        )
        output_report.write_text(gprof_result.stdout, encoding="utf-8")

        raw_flat = parse_flat_profile(gprof_result.stdout)
        raw_call_graph = parse_call_graph(gprof_result.stdout)

        filtered_flat = [row for row in raw_flat if not is_noise_symbol(row["name"])]
        filtered_call_graph = [row for row in raw_call_graph if not is_noise_symbol(row["name"])]

        payload = {
            "profile": "search_cpu",
            "profiler": "gprof",
            "search_benchmark": str(benchmark_path),
            "index_dir": str(index_dir),
            "queries_file": str(queries_file),
            "query_count": len(queries),
            "iterations_per_query": args.iterations,
            "warmup_iterations": args.warmup,
            "shard_count": shard_count(index_dir),
            "query_list_path": str(output_query_list),
            "benchmark_output_path": str(output_benchmark),
            "gprof_report_path": str(output_report),
            "flat_profile": raw_flat,
            "call_graph": raw_call_graph,
            "top_self_functions": filtered_flat[:20],
            "top_total_functions": filtered_call_graph[:20],
        }
        output_json.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")

        shutil.copy2(gmon_path, output_report.with_suffix(".gmon.out"))

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
