#!/usr/bin/env python3

from __future__ import annotations

import argparse
import collections
import concurrent.futures
import json
import math
import pathlib
import random
import statistics
import time
import urllib.error
import urllib.parse
import urllib.request

from query_fixture import SearchQuerySpec, load_search_queries


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--server-url", default="https://theavengine.com")
    parser.add_argument(
        "--queries-file",
        default=str(pathlib.Path(__file__).resolve().parents[1] / "fixtures" / "live_stress_queries.tsv"),
    )
    parser.add_argument("--qps", type=float, default=10.0)
    parser.add_argument("--duration", type=float, default=30.0)
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--limit", type=int, default=10)
    parser.add_argument("--timeout", type=float, default=8.0)
    parser.add_argument("--max-workers", type=int, default=48)
    parser.add_argument("--seed", type=int, default=498)
    parser.add_argument(
        "--output",
        default=str(pathlib.Path("artifacts") / "live_search_stress.json"),
    )
    return parser.parse_args()


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


def sample_status_code(exc: urllib.error.HTTPError | None) -> int | None:
    if exc is None:
        return None
    try:
        return int(exc.code)
    except Exception:
        return None


def query_once(server_url: str, query: str, limit: int, timeout_s: float) -> dict:
    params = urllib.parse.urlencode({"q": query, "limit": limit})
    url = f"{server_url.rstrip('/')}/api/search?{params}"
    started = time.time()
    start_perf = time.perf_counter()

    try:
        with urllib.request.urlopen(url, timeout=timeout_s) as response:
            payload = response.read()
            status = int(response.status)
    except urllib.error.HTTPError as exc:
        body = exc.read()
        elapsed_ms = (time.perf_counter() - start_perf) * 1000.0
        message = body.decode("utf-8", errors="replace").strip()
        return {
            "query": query,
            "started_at": started,
            "client_ms": elapsed_ms,
            "status_code": sample_status_code(exc),
            "outcome": "http_error",
            "error_message": message[:500],
        }
    except urllib.error.URLError as exc:
        elapsed_ms = (time.perf_counter() - start_perf) * 1000.0
        reason = str(exc.reason)
        outcome = "timeout" if "timed out" in reason.lower() else "network_error"
        return {
            "query": query,
            "started_at": started,
            "client_ms": elapsed_ms,
            "status_code": None,
            "outcome": outcome,
            "error_message": reason,
        }
    except TimeoutError as exc:
        elapsed_ms = (time.perf_counter() - start_perf) * 1000.0
        return {
            "query": query,
            "started_at": started,
            "client_ms": elapsed_ms,
            "status_code": None,
            "outcome": "timeout",
            "error_message": str(exc),
        }

    elapsed_ms = (time.perf_counter() - start_perf) * 1000.0

    try:
        parsed = json.loads(payload)
    except json.JSONDecodeError as exc:
        return {
            "query": query,
            "started_at": started,
            "client_ms": elapsed_ms,
            "status_code": status,
            "outcome": "invalid_json",
            "error_message": str(exc),
        }

    errors = parsed.get("errors", [])
    outcome = "backend_error" if errors else "ok"
    return {
        "query": query,
        "started_at": started,
        "client_ms": elapsed_ms,
        "status_code": status,
        "outcome": outcome,
        "error_count": len(errors),
        "error_messages": [error.get("message", "") for error in errors if isinstance(error, dict)],
        "took_ms": int(parsed.get("took_ms", 0)),
        "parse_ms": int(parsed.get("parse_ms", 0)),
        "solve_ms": int(parsed.get("solve_ms", 0)),
        "collect_ms": int(parsed.get("collect_ms", 0)),
        "rank_ms": int(parsed.get("rank_ms", 0)),
        "total_hits": int(parsed.get("total_hits", 0)),
        "result_count": len(parsed.get("results", [])),
    }


def summarize_run(run_id: int, duration_s: float, requested_qps: float, samples: list[dict]) -> dict:
    launched = len(samples)
    outcomes = collections.Counter(sample["outcome"] for sample in samples)
    latencies = [float(sample["client_ms"]) for sample in samples]
    server_took = [float(sample.get("took_ms", 0)) for sample in samples if sample["outcome"] in ("ok", "backend_error")]
    backend_errors = [sample for sample in samples if sample["outcome"] == "backend_error"]
    timed_out_queries = collections.Counter(sample["query"] for sample in samples if sample["outcome"] == "timeout")
    failed_queries = collections.Counter(sample["query"] for sample in samples if sample["outcome"] != "ok")

    return {
        "run": run_id,
        "duration_s": duration_s,
        "requested_qps": requested_qps,
        "launched_requests": launched,
        "completed_requests": launched,
        "achieved_qps": launched / duration_s if duration_s > 0 else 0.0,
        "outcomes": dict(outcomes),
        "success_count": outcomes.get("ok", 0),
        "timeout_count": outcomes.get("timeout", 0),
        "http_error_count": outcomes.get("http_error", 0),
        "invalid_json_count": outcomes.get("invalid_json", 0),
        "network_error_count": outcomes.get("network_error", 0),
        "backend_error_count": outcomes.get("backend_error", 0),
        "mean_client_ms": statistics.fmean(latencies) if latencies else 0.0,
        "p50_client_ms": percentile(latencies, 0.50),
        "p95_client_ms": percentile(latencies, 0.95),
        "max_client_ms": max(latencies) if latencies else 0.0,
        "mean_server_took_ms": statistics.fmean(server_took) if server_took else 0.0,
        "most_common_timeout_query": timed_out_queries.most_common(1)[0][0] if timed_out_queries else None,
        "most_common_failed_query": failed_queries.most_common(1)[0][0] if failed_queries else None,
        "top_timeout_queries": timed_out_queries.most_common(10),
        "top_failed_queries": failed_queries.most_common(10),
        "top_backend_error_messages": collections.Counter(
            message
            for sample in backend_errors
            for message in sample.get("error_messages", [])
            if message
        ).most_common(10),
    }


def aggregate_query_stats(samples: list[dict]) -> list[dict]:
    grouped: dict[str, list[dict]] = collections.defaultdict(list)
    for sample in samples:
        grouped[sample["query"]].append(sample)

    rows: list[dict] = []
    for query, query_samples in grouped.items():
        latencies = [float(sample["client_ms"]) for sample in query_samples]
        outcomes = collections.Counter(sample["outcome"] for sample in query_samples)
        rows.append(
            {
                "query": query,
                "count": len(query_samples),
                "success_count": outcomes.get("ok", 0),
                "timeout_count": outcomes.get("timeout", 0),
                "backend_error_count": outcomes.get("backend_error", 0),
                "invalid_json_count": outcomes.get("invalid_json", 0),
                "http_error_count": outcomes.get("http_error", 0),
                "network_error_count": outcomes.get("network_error", 0),
                "mean_client_ms": statistics.fmean(latencies) if latencies else 0.0,
                "p95_client_ms": percentile(latencies, 0.95),
            }
        )

    rows.sort(key=lambda row: (-row["timeout_count"], -row["backend_error_count"], -row["mean_client_ms"], row["query"]))
    return rows


def run_once(
    run_id: int,
    server_url: str,
    queries: list[SearchQuerySpec],
    qps: float,
    duration_s: float,
    limit: int,
    timeout_s: float,
    max_workers: int,
    rng: random.Random,
) -> tuple[list[dict], dict]:
    total_requests = max(1, int(math.floor(qps * duration_s)))
    spacing_s = 1.0 / qps if qps > 0 else duration_s
    run_start = time.perf_counter()
    futures: list[concurrent.futures.Future[dict]] = []
    samples: list[dict] = []

    with concurrent.futures.ThreadPoolExecutor(max_workers=max_workers) as executor:
        for index in range(total_requests):
            deadline = run_start + (index * spacing_s)
            now = time.perf_counter()
            if deadline > now:
                time.sleep(deadline - now)

            spec = queries[rng.randrange(len(queries))]
            futures.append(
                executor.submit(query_once, server_url, spec.query, limit or spec.limit, timeout_s)
            )

        for future in concurrent.futures.as_completed(futures):
            samples.append(future.result())

    summary = summarize_run(run_id, duration_s, qps, samples)
    return samples, summary


def build_overall_summary(args: argparse.Namespace, all_samples: list[dict], run_summaries: list[dict]) -> dict:
    outcomes = collections.Counter(sample["outcome"] for sample in all_samples)
    latencies = [float(sample["client_ms"]) for sample in all_samples]
    server_took = [float(sample.get("took_ms", 0)) for sample in all_samples if sample["outcome"] in ("ok", "backend_error")]
    timeout_queries = collections.Counter(sample["query"] for sample in all_samples if sample["outcome"] == "timeout")
    failed_queries = collections.Counter(sample["query"] for sample in all_samples if sample["outcome"] != "ok")
    slow_queries = sorted(
        aggregate_query_stats(all_samples),
        key=lambda row: (-row["mean_client_ms"], -row["timeout_count"], row["query"]),
    )

    return {
        "server_url": args.server_url,
        "queries_file": str(pathlib.Path(args.queries_file).resolve()),
        "requested_qps": args.qps,
        "duration_s": args.duration,
        "runs": args.runs,
        "limit": args.limit,
        "timeout_s": args.timeout,
        "seed": args.seed,
        "launched_requests": len(all_samples),
        "completed_requests": len(all_samples),
        "outcomes": dict(outcomes),
        "success_count": outcomes.get("ok", 0),
        "timeout_count": outcomes.get("timeout", 0),
        "http_error_count": outcomes.get("http_error", 0),
        "invalid_json_count": outcomes.get("invalid_json", 0),
        "network_error_count": outcomes.get("network_error", 0),
        "backend_error_count": outcomes.get("backend_error", 0),
        "success_rate": (outcomes.get("ok", 0) / len(all_samples)) if all_samples else 0.0,
        "mean_client_ms": statistics.fmean(latencies) if latencies else 0.0,
        "p50_client_ms": percentile(latencies, 0.50),
        "p95_client_ms": percentile(latencies, 0.95),
        "max_client_ms": max(latencies) if latencies else 0.0,
        "mean_server_took_ms": statistics.fmean(server_took) if server_took else 0.0,
        "most_common_timeout_query": timeout_queries.most_common(1)[0][0] if timeout_queries else None,
        "most_common_failed_query": failed_queries.most_common(1)[0][0] if failed_queries else None,
        "top_timeout_queries": timeout_queries.most_common(10),
        "top_failed_queries": failed_queries.most_common(10),
        "top_slowest_queries": slow_queries[:10],
        "per_query": aggregate_query_stats(all_samples),
        "run_summaries": run_summaries,
    }


def print_summary(summary: dict) -> None:
    print("Live search stress summary")
    print(f"  Server:           {summary['server_url']}")
    print(f"  Runs:             {summary['runs']}")
    print(f"  Duration/run:     {summary['duration_s']}s")
    print(f"  Requested QPS:    {summary['requested_qps']}")
    print(f"  Requests:         {summary['completed_requests']} completed")
    print(f"  Successes:        {summary['success_count']}")
    print(f"  Timeouts:         {summary['timeout_count']}")
    print(f"  HTTP errors:      {summary['http_error_count']}")
    print(f"  Invalid JSON:     {summary['invalid_json_count']}")
    print(f"  Backend errors:   {summary['backend_error_count']}")
    print(f"  Network errors:   {summary['network_error_count']}")
    print(f"  Mean client ms:   {summary['mean_client_ms']:.1f}")
    print(f"  P50 client ms:    {summary['p50_client_ms']:.1f}")
    print(f"  P95 client ms:    {summary['p95_client_ms']:.1f}")
    print(f"  Max client ms:    {summary['max_client_ms']:.1f}")
    print(f"  Mean server ms:   {summary['mean_server_took_ms']:.1f}")
    print(f"  Common timeout:   {summary['most_common_timeout_query']}")
    print(f"  Common failure:   {summary['most_common_failed_query']}")
    print("  Top timeout queries:")
    for query, count in summary["top_timeout_queries"][:5]:
        print(f"    {count:>3}  {query}")
    print("  Top slow queries:")
    for row in summary["top_slowest_queries"][:5]:
        print(f"    {row['mean_client_ms']:>7.1f} ms  {row['query']}")


def main() -> int:
    args = parse_args()
    queries = load_search_queries(args.queries_file)
    output_path = pathlib.Path(args.output).resolve()
    output_path.parent.mkdir(parents=True, exist_ok=True)

    rng = random.Random(args.seed)
    all_samples: list[dict] = []
    run_summaries: list[dict] = []

    for run_id in range(1, args.runs + 1):
        samples, summary = run_once(
            run_id=run_id,
            server_url=args.server_url,
            queries=queries,
            qps=args.qps,
            duration_s=args.duration,
            limit=args.limit,
            timeout_s=args.timeout,
            max_workers=args.max_workers,
            rng=rng,
        )
        all_samples.extend(samples)
        run_summaries.append(summary)
        print(
            f"run {run_id}: requests={summary['completed_requests']} ok={summary['success_count']} "
            f"timeouts={summary['timeout_count']} backend_errors={summary['backend_error_count']} "
            f"p95={summary['p95_client_ms']:.1f}ms"
        )

    summary = build_overall_summary(args, all_samples, run_summaries)
    payload = {
        "profile": "live_search_stress",
        "summary": summary,
        "samples": all_samples,
    }
    output_path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    print_summary(summary)
    print(f"Wrote {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
