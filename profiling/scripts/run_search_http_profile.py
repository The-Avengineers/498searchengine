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


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--index-builder", required=True)
    parser.add_argument("--search-server", required=True)
    parser.add_argument("--fixture", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--iterations", type=int, default=12)
    parser.add_argument("--port", type=int)
    return parser.parse_args()


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


def main() -> int:
    args = parse_args()
    repo_root = pathlib.Path(__file__).resolve().parents[2]
    fixture_path = pathlib.Path(args.fixture).resolve()
    output_path = pathlib.Path(args.output).resolve()
    output_path.parent.mkdir(parents=True, exist_ok=True)

    queries = [
        # Single common term — many hits, exercises ISR decode + collect
        {"query": "apple", "limit": 10, "expected_total_hits": 4},
        # Multi-term AND — intersection, smaller hit set
        {"query": "banana cherry", "limit": 10, "expected_total_hits": 2},
        # Phrase query — exact adjacency matching, exercises ISRPhrase
        {"query": "\"distributed search ranking\"", "limit": 10, "expected_total_hits": 1},
        # No-results query — exercises early-exit path
        {"query": "kiwi", "limit": 10, "expected_total_hits": 0},
        # Single term with a single hit
        {"query": "telemetry", "limit": 10, "expected_total_hits": 1},
        # Multi-word phrase covering multiple shards
        {"query": "search ranking", "limit": 10, "expected_total_hits": 3},
    ]

    samples: list[dict] = []
    with tempfile.TemporaryDirectory(prefix="avengine_search_profile_") as tmpdir:
        tmpdir_path = pathlib.Path(tmpdir)

        subprocess.run(
            [os.path.abspath(args.index_builder), str(fixture_path)],
            cwd=tmpdir,
            check=True,
            capture_output=True,
            text=True,
        )

        shards = sorted(tmpdir_path.glob("*.sfi"))
        if not shards:
            raise RuntimeError("index_builder did not produce any .sfi shards")

        server = None
        port = None
        last_error: Exception | None = None
        for candidate in candidate_ports(args.port):
            server = subprocess.Popen(
                [os.path.abspath(args.search_server), str(candidate), tmpdir],
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
            for spec in queries:
                warmup = query_once(port, spec["query"], spec["limit"])
                if warmup["response"]["total_hits"] != spec["expected_total_hits"]:
                    raise RuntimeError(
                        f"Warmup mismatch for query {spec['query']!r}: "
                        f"expected {spec['expected_total_hits']}, got {warmup['response']['total_hits']}"
                    )

                latencies: list[float] = []
                server_took_ms: list[int] = []
                parse_ms: list[int] = []
                solve_ms: list[int] = []
                collect_ms: list[int] = []
                rank_ms: list[int] = []
                for _ in range(args.iterations):
                    sample = query_once(port, spec["query"], spec["limit"])
                    response = sample["response"]
                    if response["total_hits"] != spec["expected_total_hits"]:
                        raise RuntimeError(
                            f"Unexpected result count for query {spec['query']!r}: "
                            f"expected {spec['expected_total_hits']}, got {response['total_hits']}"
                        )
                    latencies.append(sample["client_ms"])
                    server_took_ms.append(int(response.get("took_ms", 0)))
                    parse_ms.append(int(response.get("parse_ms", 0)))
                    solve_ms.append(int(response.get("solve_ms", 0)))
                    collect_ms.append(int(response.get("collect_ms", 0)))
                    rank_ms.append(int(response.get("rank_ms", 0)))

                samples.append(
                    {
                        "query": spec["query"],
                        "iterations": args.iterations,
                        "expected_total_hits": spec["expected_total_hits"],
                        "mean_client_ms": statistics.fmean(latencies),
                        "p50_client_ms": percentile(latencies, 0.50),
                        "p95_client_ms": percentile(latencies, 0.95),
                        "mean_server_took_ms": statistics.fmean(server_took_ms),
                        "max_server_took_ms": max(server_took_ms),
                        "mean_parse_ms": statistics.fmean(parse_ms),
                        "mean_solve_ms": statistics.fmean(solve_ms),
                        "mean_collect_ms": statistics.fmean(collect_ms),
                        "mean_rank_ms": statistics.fmean(rank_ms),
                    }
                )
        finally:
            if server is not None:
                server.terminate()
                try:
                    server.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    server.kill()
                    server.wait(timeout=5)

        all_latencies = [sample["mean_client_ms"] for sample in samples]
        payload = {
            "profile": "search_http",
            "fixture": str(fixture_path),
            "port": port,
            "iterations_per_query": args.iterations,
            "query_count": len(samples),
            "shard_count": len(shards),
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
