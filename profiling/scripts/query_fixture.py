#!/usr/bin/env python3

from __future__ import annotations

import csv
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class SearchQuerySpec:
    label: str
    query: str
    limit: int
    group: str = "top"


def load_search_queries(path: str | Path) -> list[SearchQuerySpec]:
    fixture_path = Path(path)
    with fixture_path.open("r", encoding="utf-8", newline="") as handle:
        reader = csv.reader(handle, delimiter="\t", quoting=csv.QUOTE_NONE)
        header = next(reader, None)
        if header is None:
            raise RuntimeError(f"query fixture is empty: {fixture_path}")

        columns = {name: idx for idx, name in enumerate(header)}
        queries: list[SearchQuerySpec] = []
        for row in reader:
            if not row:
                continue

            query = (row[columns["query"]] if columns["query"] < len(row) else "").strip()
            if not query:
                continue

            label = (row[columns["label"]] if columns["label"] < len(row) else query).strip() or query
            limit_text = (row[columns["limit"]] if columns["limit"] < len(row) else "10").strip()
            group = (
                row[columns["group"]].strip()
                if "group" in columns and columns["group"] < len(row)
                else "top"
            ) or "top"

            queries.append(
                SearchQuerySpec(
                    label=label,
                    query=query,
                    limit=int(limit_text),
                    group=group,
                )
            )

    if not queries:
        raise RuntimeError(f"no queries found in {fixture_path}")
    return queries
