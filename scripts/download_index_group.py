#!/usr/bin/env python3
"""Download all index shard files for the group containing a chunk id.

The remote host exposes:
- directory listings at the base URL
- group manifests in `crawler-0-disk/index_group_XXX.txt`
- shard files in `crawler-0-disk/` and `crawler-1-disk/`

Given a chunk id like `24` or `00024`, this script finds the manifest that
contains that id and downloads every file listed in that manifest into a local
output directory while preserving the remote disk directory and original file
name, e.g.:

    out/
      crawler-0-disk/00024.sfi
      crawler-0-disk/anchor_00024.sfi
      crawler-1-disk/00024.sfi
      crawler-1-disk/anchor_00024.sfi

The matching manifest is also saved locally.
"""

from __future__ import annotations

import argparse
import html.parser
import re
import sys
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass
from pathlib import Path

GROUP_RE = re.compile(r"index_group_(\d{3})\.txt$")


class LinkParser(html.parser.HTMLParser):
    def __init__(self) -> None:
        super().__init__()
        self.links: list[str] = []

    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        if tag != "a":
            return
        for key, value in attrs:
            if key == "href" and value is not None:
                self.links.append(value)


@dataclass(frozen=True)
class ManifestEntry:
    archive_name: str
    source_path: str
    size_bytes: int
    chunk_id: str

    @property
    def disk_dir(self) -> str:
        match = re.search(r"/mnt/(crawler-[^/]+)/", self.source_path)
        if not match:
            raise ValueError(f"Could not determine disk directory from {self.source_path}")
        return match.group(1)

    @property
    def remote_filename(self) -> str:
        return Path(self.source_path).name

    @property
    def remote_relative_path(self) -> str:
        return f"{self.disk_dir}/{self.remote_filename}"

    @property
    def local_relative_path(self) -> Path:
        return Path(self.disk_dir) / self.remote_filename


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Download all files for the index group containing a given chunk id"
    )
    parser.add_argument("chunk_id", help="Chunk id to look up, e.g. 24 or 00024")
    parser.add_argument(
        "--base-url",
        default="http://34.24.237.188/",
        help="Base URL hosting crawler disks and group manifests",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=Path("downloaded-index-group"),
        help="Directory where files will be downloaded",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Print the matching manifest and files without downloading",
    )
    return parser.parse_args()


def normalize_chunk_id(raw: str) -> str:
    raw = raw.strip()
    if not raw.isdigit():
        raise ValueError(f"Chunk id must be numeric, got: {raw!r}")
    value = int(raw)
    if value < 0 or value > 99999:
        raise ValueError(f"Chunk id must be between 0 and 99999, got: {value}")
    return f"{value:05d}"


def fetch_text(url: str) -> str:
    with urllib.request.urlopen(url) as response:
        return response.read().decode("utf-8")


def list_group_manifests(base_url: str) -> list[str]:
    listing_url = urllib.parse.urljoin(base_url.rstrip("/") + "/", "crawler-0-disk/")
    html = fetch_text(listing_url)
    parser = LinkParser()
    parser.feed(html)
    manifests = sorted({link for link in parser.links if GROUP_RE.search(link)})
    if not manifests:
        raise RuntimeError(f"No index_group_XXX.txt manifests found at {listing_url}")
    return manifests


def parse_manifest(text: str) -> list[ManifestEntry]:
    entries: list[ManifestEntry] = []
    for line_no, raw_line in enumerate(text.splitlines(), start=1):
        line = raw_line.strip()
        if not line:
            continue
        parts = line.split("\t")
        if len(parts) != 4:
            raise ValueError(f"Invalid manifest line {line_no}: {raw_line!r}")
        archive_name, source_path, size_text, chunk_id = parts
        entries.append(
            ManifestEntry(
                archive_name=archive_name,
                source_path=source_path,
                size_bytes=int(size_text),
                chunk_id=chunk_id,
            )
        )
    return entries


def find_manifest_for_chunk(base_url: str, chunk_id: str) -> tuple[str, list[ManifestEntry]]:
    for manifest_name in list_group_manifests(base_url):
        manifest_url = urllib.parse.urljoin(
            base_url.rstrip("/") + "/", f"crawler-0-disk/{manifest_name}"
        )
        entries = parse_manifest(fetch_text(manifest_url))
        if any(entry.chunk_id == chunk_id for entry in entries):
            return manifest_name, entries
    raise RuntimeError(f"No manifest contains chunk id {chunk_id}")


def download_file(url: str, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    with urllib.request.urlopen(url) as response, destination.open("wb") as out:
        while True:
            block = response.read(1024 * 1024)
            if not block:
                break
            out.write(block)


def main() -> int:
    args = parse_args()

    try:
        chunk_id = normalize_chunk_id(args.chunk_id)
        base_url = args.base_url.rstrip("/") + "/"
        manifest_name, entries = find_manifest_for_chunk(base_url, chunk_id)
    except (ValueError, RuntimeError, urllib.error.URLError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    manifest_url = urllib.parse.urljoin(base_url, f"crawler-0-disk/{manifest_name}")
    output_dir: Path = args.output_dir

    print(f"Chunk id: {chunk_id}")
    print(f"Manifest: {manifest_name}")
    print(f"Manifest URL: {manifest_url}")
    print(f"Files in group: {len(entries)}")

    if args.dry_run:
        for entry in entries:
            print(f"  {entry.remote_relative_path} -> {entry.local_relative_path}")
        return 0

    try:
        output_dir.mkdir(parents=True, exist_ok=True)
        manifest_destination = output_dir / manifest_name
        download_file(manifest_url, manifest_destination)
        print(f"Saved manifest to {manifest_destination}")

        for entry in entries:
            file_url = urllib.parse.urljoin(base_url, entry.remote_relative_path)
            destination = output_dir / entry.local_relative_path
            print(f"Downloading {file_url} -> {destination}")
            download_file(file_url, destination)
    except (OSError, urllib.error.URLError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    print(f"Done. Downloaded group {manifest_name} into {output_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
