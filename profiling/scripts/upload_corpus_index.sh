#!/usr/bin/env bash
# upload_corpus_index.sh — publish .sfi index shards to a GitHub Release
#
# Usage:
#   bash profiling/scripts/upload_corpus_index.sh [INDEX_DIR]
#
# INDEX_DIR defaults to the repo root (where the .sfi files are written after
# a crawl run). Uploads all *.sfi files to the release tagged
# 'corpus-index-latest', replacing any previous assets.
#
# Requires: gh CLI authenticated with write access to the repo.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

INDEX_DIR="${1:-${REPO_ROOT}}"
TAG="corpus-index-latest"

if ! command -v gh &>/dev/null; then
  echo "gh CLI is required. Install from https://cli.github.com/" >&2
  exit 1
fi

SHARDS=()
for f in "${INDEX_DIR}"/*.sfi; do
  [[ -f "${f}" ]] && SHARDS+=("${f}")
done
if [[ ${#SHARDS[@]} -eq 0 ]]; then
  echo "No .sfi files found in ${INDEX_DIR}" >&2
  exit 1
fi

echo "Found ${#SHARDS[@]} shard(s) in ${INDEX_DIR}:"
for shard in "${SHARDS[@]}"; do
  size_mb=$(( $(wc -c < "${shard}") / 1048576 ))
  printf "  %s (%d MB)\n" "$(basename "${shard}")" "${size_mb}"
done

echo
echo "Uploading to GitHub Release '${TAG}'..."

# Delete existing release/tag so assets are fully replaced.
gh release delete "${TAG}" --yes 2>/dev/null || true
git tag -d "${TAG}" 2>/dev/null || true
git push origin ":refs/tags/${TAG}" 2>/dev/null || true

gh release create "${TAG}" \
  --title "Corpus Index" \
  --notes "Large crawl index for CI profiling. Generated from a real web crawl. Do not commit these files to the repository." \
  "${SHARDS[@]}"

echo "Done. CI will download these shards using: gh release download ${TAG} --pattern '*.sfi'"
