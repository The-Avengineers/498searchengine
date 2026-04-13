#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import os
import pathlib
import socket
import statistics
import subprocess
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request

from query_fixture import load_search_queries


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--index-builder")
    parser.add_argument("--index-dir")
    parser.add_argument("--search-server")
    parser.add_argument("--live-server-port", type=int,
                        help="connect to an already-running search server on this port instead of "
                             "starting a new one")
    parser.add_argument("--fixture")
    parser.add_argument("--queries-file")
    parser.add_argument("--output", required=True)
    parser.add_argument("--iterations", type=int, default=12)
    parser.add_argument("--port", type=int)
    args = parser.parse_args()
    if args.live_server_port is None:
        if not args.search_server:
            parser.error("--search-server is required when not using --live-server-port")
        if not args.index_dir and not args.index_builder:
            parser.error("one of --index-dir or --index-builder is required")
        if not args.index_dir and not args.fixture:
            parser.error("--fixture is required when building a fresh index")
    return args


def query_hit_value(sample: dict) -> float:
    return float(sample.get("mean_total_hits", sample.get("total_hits", 0)))


def wait_for_port(server: subprocess.Popen[str], port: int, timeout_s: float = 10.0) -> None:
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        if server.poll() is not None:
            output = ""
            if server.stdout is not None:
                output = server.stdout.read()
            raise RuntimeError(f"search_server exited before startup:\n{output}")

        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
            sock.settimeout(0.25)
            if sock.connect_ex(("127.0.0.1", port)) == 0:
                return
        time.sleep(0.1)

    raise RuntimeError(f"search_server did not start on port {port}")


def candidate_ports(requested_port: int | None) -> list[int]:
    if requested_port is not None:
        return [requested_port]
    base = 20000 + (os.getpid() % 20000)
    return [base + offset for offset in range(16)]


def query_once(port: int, query: str, limit: int) -> dict:
    url = f"http://127.0.0.1:{port}/api/search?" + urllib.parse.urlencode({"q": query, "limit": limit})
    start = time.perf_counter()
    with urllib.request.urlopen(url, timeout=5) as response:
        payload = response.read()
    elapsed_ms = (time.perf_counter() - start) * 1000.0
    parsed = json.loads(payload)
    return {"client_ms": elapsed_ms, "response": parsed}


def percentile(values: list[float], pct: float) -> float:
    if not values:
        return 0.0
    ordered = sorted(values)
    if len(ordered) == 1:
        return ordered[0]
    rank = pct * (len(ordered) - 1)
    lower = int(rank)
    upper = min(lower + 1, len(ordered) - 1)
    fraction = rank - lower
    return ordered[lower] + (ordered[upper] - ordered[lower]) * fraction


def run_queries(port: int, queries: list, iterations: int) -> list[dict]:
    samples: list[dict] = []
    for spec in queries:
        query_once(port, spec.query, spec.limit)  # warmup

        latencies: list[float] = []
        server_took_ms: list[int] = []
        parse_ms: list[int] = []
        solve_ms: list[int] = []
        collect_ms: list[int] = []
        rank_ms: list[int] = []
        error_counts: list[int] = []
        actual_hits: list[int] = []
        for _ in range(iterations):
            sample = query_once(port, spec.query, spec.limit)
            response = sample["response"]
            total_hits = int(response.get("total_hits", 0))
            latencies.append(sample["client_ms"])
            server_took_ms.append(int(response.get("took_ms", 0)))
            parse_ms.append(int(response.get("parse_ms", 0)))
            solve_ms.append(int(response.get("solve_ms", 0)))
            collect_ms.append(int(response.get("collect_ms", 0)))
            rank_ms.append(int(response.get("rank_ms", 0)))
            error_counts.append(len(response.get("errors", [])))
            actual_hits.append(total_hits)

        samples.append(
            {
                "label": spec.label,
                "query": spec.query,
                "group": spec.group,
                "limit": spec.limit,
                "iterations": iterations,
                "mean_total_hits": statistics.fmean(actual_hits),
                "mean_client_ms": statistics.fmean(latencies),
                "p50_client_ms": percentile(latencies, 0.50),
                "p95_client_ms": percentile(latencies, 0.95),
                "mean_server_took_ms": statistics.fmean(server_took_ms),
                "max_server_took_ms": max(server_took_ms),
                "mean_parse_ms": statistics.fmean(parse_ms),
                "mean_solve_ms": statistics.fmean(solve_ms),
                "mean_collect_ms": statistics.fmean(collect_ms),
                "mean_rank_ms": statistics.fmean(rank_ms),
                "mean_error_count": statistics.fmean(error_counts),
                "max_error_count": max(error_counts),
            }
        )
    return samples


def main() -> int:
    args = parse_args()
    repo_root = pathlib.Path(__file__).resolve().parents[2]
    fixture_path = pathlib.Path(args.fixture).resolve() if args.fixture else None
    queries_path = pathlib.Path(args.queries_file).resolve() if args.queries_file else (
        repo_root / "profiling" / "fixtures" / "search_queries.tsv"
    )
    output_path = pathlib.Path(args.output).resolve()
    output_path.parent.mkdir(parents=True, exist_ok=True)
    queries = load_search_queries(queries_path)

    # -- live server mode: connect to an already-running distributed search server --
    if args.live_server_port is not None:
        port = args.live_server_port
        print(f"Connecting to live search server on port {port}")
        samples = run_queries(port, queries, args.iterations)
        all_latencies = [s["mean_client_ms"] for s in samples]
        payload = {
            "profile": "search_http",
            "live_server": True,
            "fixture": None,
            "index_dir": None,
            "queries_file": str(queries_path),
            "port": port,
            "iterations_per_query": args.iterations,
            "query_count": len(samples),
            "shard_count": 0,
            "nonzero_hit_query_count": sum(1 for s in samples if query_hit_value(s) > 0),
            "mean_client_ms": statistics.fmean(all_latencies) if all_latencies else 0.0,
            "p50_client_ms": percentile(all_latencies, 0.50),
            "p95_client_ms": percentile(all_latencies, 0.95),
            "samples": samples,
        }
        output_path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
        print(f"search_http_profile wrote {output_path}")
        return 0

    # -- managed server mode: build index (if needed) and start a fresh server --
    samples: list[dict] = []
    with tempfile.TemporaryDirectory(prefix="avengine_search_profile_") as tmpdir:
        if args.index_dir:
            index_dir = pathlib.Path(args.index_dir).resolve()
        else:
            index_dir = pathlib.Path(tmpdir)
            subprocess.run(
                [os.path.abspath(args.index_builder), str(fixture_path)],
                cwd=index_dir,
                check=True,
                capture_output=True,
                text=True,
            )

        shards = sorted(index_dir.glob("*.sfi"))
        if not shards:
            raise RuntimeError(f"no .sfi shards found in {index_dir}")

        server = None
        port = None
        last_error: Exception | None = None
        for candidate in candidate_ports(args.port):
            server = subprocess.Popen(
                [os.path.abspath(args.search_server), str(candidate), str(index_dir)],
                cwd=repo_root,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
            )
            try:
                wait_for_port(server, candidate)
                port = candidate
                break
            except RuntimeError as exc:
                last_error = exc
                if server.poll() is None:
                    server.terminate()
                    try:
                        server.wait(timeout=2)
                    except subprocess.TimeoutExpired:
                        server.kill()
                        server.wait(timeout=2)
                server = None

        if server is None or port is None:
            raise RuntimeError(f"Unable to start search_server: {last_error}")

        try:
            samples = run_queries(port, queries, args.iterations)
        finally:
            if server is not None:
                server.terminate()
                try:
                    server.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    server.kill()
                    server.wait(timeout=5)

        all_latencies = [s["mean_client_ms"] for s in samples]
        payload = {
            "profile": "search_http",
            "live_server": False,
            "fixture": str(fixture_path) if fixture_path else None,
            "index_dir": str(index_dir),
            "queries_file": str(queries_path),
            "port": port,
            "iterations_per_query": args.iterations,
            "query_count": len(samples),
            "shard_count": len(shards),
            "nonzero_hit_query_count": sum(1 for s in samples if query_hit_value(s) > 0),
            "mean_client_ms": statistics.fmean(all_latencies) if all_latencies else 0.0,
            "p50_client_ms": percentile(all_latencies, 0.50),
            "p95_client_ms": percentile(all_latencies, 0.95),
            "samples": samples,
        }

    output_path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    print(f"search_http_profile wrote {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
