#!/usr/bin/env bash
set -euo pipefail

DATA_ROOT="${AVENGINE_DATA_ROOT:-/data}"

# Analyse a crawl_log and emit: total<tab>errors<tab>mb<tab>rate/s<tab>last_url<tab>last_ts
_crawl_stats() {
  python3 - "$1" <<'PYEOF'
import sys, datetime, pathlib

path = pathlib.Path(sys.argv[1])
lines = [l for l in path.read_text(errors="replace").splitlines() if l.strip()]

total = errors = bytes_total = 0
last_url = last_ts = ""

for line in lines:
    total += 1
    parts = line.split(",")
    if len(parts) >= 5 and parts[2] == " Code 200":
        try:
            bytes_total += int(parts[4])
        except (ValueError, IndexError):
            pass
        last_url = parts[3]
        last_ts  = parts[0]
    else:
        errors  += 1
        last_url = parts[-1].strip() if parts else ""
        last_ts  = parts[0] if parts else ""

rate = 0.0
recent = lines[-200:]
if len(recent) > 1:
    try:
        t1 = datetime.datetime.fromisoformat(recent[0].split(",")[0].replace("Z", "+00:00"))
        t2 = datetime.datetime.fromisoformat(recent[-1].split(",")[0].replace("Z", "+00:00"))
        secs = (t2 - t1).total_seconds()
        if secs > 0:
            rate = len(recent) / secs
    except (ValueError, AttributeError):
        pass

mb = bytes_total / (1024 * 1024)
print(f"{total}\t{errors}\t{mb:.0f}\t{rate:.1f}\t{last_url}\t{last_ts}")
PYEOF
}

# ── Per-worker stats ──────────────────────────────────────────────────────────
printf "%-10s %-10s %-7s %-10s %-8s %-8s %-8s\n" \
  "worker" "state" "shards" "crawled" "errors" "mb" "rate/s"

declare -a LAST_CRAWLED=()
tot_crawled=0; tot_errors=0; tot_mb=0

for worker_dir in "${DATA_ROOT}"/crawlers/*; do
  [[ -d "${worker_dir}" ]] || continue
  worker_id="$(basename "${worker_dir}")"
  pid_file="${DATA_ROOT}/pids/crawler-${worker_id}.pid"
  state="stopped"

  if [[ -f "${pid_file}" ]]; then
    pid="$(cat "${pid_file}")"
    if kill -0 "${pid}" 2>/dev/null; then
      state="running"
    else
      state="stale-pid"
    fi
  fi

  shards="$(find "${worker_dir}" -maxdepth 1 -type f -name '*.sfi' | wc -l | tr -d ' ')"

  crawled=0; err=0; mb=0; rate="--"; last_url=""; last_ts=""
  if [[ -f "${worker_dir}/crawl_log.csv" ]]; then
    IFS=$'\t' read -r crawled err mb rate last_url last_ts \
      < <(_crawl_stats "${worker_dir}/crawl_log.csv" 2>/dev/null \
          || printf "0\t0\t0\t0.0\t\t")
  fi

  printf "%-10s %-10s %-7s %-10s %-8s %-8s %-8s\n" \
    "${worker_id}" "${state}" "${shards}" "${crawled}" "${err}" "${mb}" "${rate}"

  tot_crawled=$(( tot_crawled + ${crawled:-0} ))
  tot_errors=$(( tot_errors  + ${err:-0}     ))
  tot_mb=$(( tot_mb          + ${mb:-0}      ))

  if [[ -n "${last_url}" ]]; then
    short="${last_url:0:65}"
    [[ "${#last_url}" -gt 65 ]] && short+="…"
    LAST_CRAWLED+=("  ${worker_id}  [${last_ts:0:19}]  ${short}")
  fi
done

# ── Recent crawl activity ─────────────────────────────────────────────────────
if (( ${#LAST_CRAWLED[@]} > 0 )); then
  echo
  echo "recent activity:"
  for entry in "${LAST_CRAWLED[@]}"; do
    echo "${entry}"
  done
fi

# ── Totals ────────────────────────────────────────────────────────────────────
echo
current_shards="$(find -L "${DATA_ROOT}/corpus-index/current" -maxdepth 1 -type f -name '*.sfi' 2>/dev/null | wc -l | tr -d ' ')"
worker_shards="$(find "${DATA_ROOT}/crawlers" -type f -name '*.sfi' 2>/dev/null | wc -l | tr -d ' ')"
printf "totals:  crawled: %s  errors: %s  mb: %s  worker shards: %s  corpus shards: %s\n" \
  "${tot_crawled}" "${tot_errors}" "${tot_mb}" "${worker_shards}" "${current_shards}"
