#!/usr/bin/env python3
"""Stripe index chunks into size-bounded groups.

This script:
- scans one or more source directories for `XXXXX.sfi` and `anchor_XXXXX.sfi`
- treats each numeric chunk ID as an indivisible unit, so `00042.sfi` and
  `anchor_00042.sfi` always land in the same group
- stripes units round-robin across groups so earlier chunks are spread across
  different outputs
- writes one text file per group listing the flattened files assigned to it
- uses estimated plain `.tar` sizes to ensure each group would fit strictly
  under the requested size limit if archived later

Each output line is tab-separated:
- column 1: flattened downloaded filename
- column 2: original source path

To avoid collisions across disks, flattened names are prefixed with a short
 disk label based on input order: `A_`, `B_`, `C_`, and so on. For the default
 two disks, names look like `A_00042.sfi` or `B_anchor_00042.sfi`.
"""

from __future__ import annotations

import argparse
import math
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterable

BLOCK_SIZE = 512
RECORD_SIZE = 20 * BLOCK_SIZE  # tarfile module pads archives to this size
INDEX_RE = re.compile(r"^(anchor_)?(\d{5})\.sfi$")


@dataclass(frozen=True)
class SourceFile:
    source_path: Path
    downloaded_name: str
    size_bytes: int

    @property
    def tar_member_size(self) -> int:
        return BLOCK_SIZE + round_up(self.size_bytes, BLOCK_SIZE)


@dataclass
class ChunkUnit:
    chunk_id: int
    files: list[SourceFile] = field(default_factory=list)

    @property
    def tar_payload_size(self) -> int:
        return sum(f.tar_member_size for f in self.files)

    @property
    def logical_size(self) -> int:
        return sum(f.size_bytes for f in self.files)


def round_up(value: int, multiple: int) -> int:
    return ((value + multiple - 1) // multiple) * multiple


def estimate_archive_size(files: Iterable[SourceFile]) -> int:
    payload = sum(f.tar_member_size for f in files)
    return round_up(payload + (2 * BLOCK_SIZE), RECORD_SIZE)


def estimate_archive_size_for_units(units: Iterable[ChunkUnit]) -> int:
    payload = sum(unit.tar_payload_size for unit in units)
    return round_up(payload + (2 * BLOCK_SIZE), RECORD_SIZE)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Stripe paired index chunks into groups under a tar-size cap"
    )
    parser.add_argument(
        "max_size_gb",
        type=float,
        help="Maximum tar size in decimal GB (1 GB = 1,000,000,000 bytes)",
    )
    parser.add_argument(
        "output_dir",
        type=Path,
        help="Directory where per-group file lists and manifest will be written",
    )
    parser.add_argument(
        "--disk",
        dest="disks",
        action="append",
        type=Path,
        help=(
            "Source disk/directory to scan. Pass multiple times. "
            "Defaults to /mnt/crawler-0-disk and /mnt/crawler-1-disk."
        ),
    )
    parser.add_argument(
        "--prefix",
        default="index_group",
        help="Output filename prefix (default: index_group)",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Compute grouping and print summary without writing group files",
    )
    return parser.parse_args()


def collect_units(disks: list[Path]) -> list[ChunkUnit]:
    units: dict[int, ChunkUnit] = {}

    for disk_index, disk in enumerate(disks):
        if not disk.exists():
            raise FileNotFoundError(f"Disk path does not exist: {disk}")
        if not disk.is_dir():
            raise NotADirectoryError(f"Disk path is not a directory: {disk}")

        disk_label = chr(ord("A") + disk_index)
        for child in disk.iterdir():
            if not child.is_file():
                continue
            match = INDEX_RE.match(child.name)
            if not match:
                continue

            chunk_id = int(match.group(2))
            unit = units.setdefault(chunk_id, ChunkUnit(chunk_id=chunk_id))
            unit.files.append(
                SourceFile(
                    source_path=child,
                    downloaded_name=f"{disk_label}_{child.name}",
                    size_bytes=child.stat().st_size,
                )
            )

    if not units:
        raise RuntimeError(
            "No index files matching `XXXXX.sfi` or `anchor_XXXXX.sfi` were found"
        )

    for unit in units.values():
        unit.files.sort(key=lambda f: f.downloaded_name)

    return sorted(units.values(), key=lambda unit: unit.chunk_id)


def choose_grouping(
    units: list[ChunkUnit], max_size_bytes: int
) -> list[list[ChunkUnit]]:
    if max_size_bytes <= 0:
        raise ValueError("Size limit must be positive")

    for unit in units:
        unit_archive_size = estimate_archive_size_for_units([unit])
        if unit_archive_size >= max_size_bytes:
            raise ValueError(
                "Single chunk unit cannot fit under limit: "
                f"chunk {unit.chunk_id:05d} needs {unit_archive_size} bytes"
            )

    total_payload = sum(unit.tar_payload_size for unit in units)
    total_estimated = round_up(total_payload + (2 * BLOCK_SIZE), RECORD_SIZE)
    group_count = min(
        len(units), max(1, math.ceil(total_estimated / max_size_bytes))
    )

    while group_count <= len(units):
        groups = [[] for _ in range(group_count)]
        for i, unit in enumerate(units):
            groups[i % group_count].append(unit)

        if all(
            estimate_archive_size_for_units(group) < max_size_bytes
            for group in groups
        ):
            return groups
        group_count += 1

    raise ValueError(
        "Could not find a valid grouping under the requested size limit"
    )


def write_group_files(
    output_dir: Path, prefix: str, groups: list[list[ChunkUnit]]
) -> None:
    for i, group in enumerate(groups):
        group_path = output_dir / f"{prefix}_{i:03d}.txt"
        lines: list[str] = []

        for unit in group:
            for source_file in unit.files:
                lines.append(
                    f"{source_file.downloaded_name}\t{source_file.source_path}"
                )

        group_path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    args = parse_args()
    disks = args.disks or [
        Path("/mnt/crawler-0-disk"),
        Path("/mnt/crawler-1-disk"),
    ]
    max_size_bytes = int(args.max_size_gb * 1_000_000_000)

    try:
        units = collect_units(disks)
        groups = choose_grouping(units, max_size_bytes)
    except Exception as exc:  # noqa: BLE001
        print(f"error: {exc}", file=sys.stderr)
        return 1

    args.output_dir.mkdir(parents=True, exist_ok=True)

    if args.dry_run:
        for i, group in enumerate(groups):
            print(
                f"{args.prefix}_{i:03d}.txt\tchunks={len(group)}\t"
                f"estimated_tar_bytes={estimate_archive_size_for_units(group)}"
            )
        return 0

    try:
        write_group_files(args.output_dir, args.prefix, groups)
    except Exception as exc:  # noqa: BLE001
        print(f"error: {exc}", file=sys.stderr)
        return 1

    print(f"Wrote {len(groups)} group files to {args.output_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
