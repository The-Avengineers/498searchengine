#!/usr/bin/env bash
set -euo pipefail

DATA_ROOT="${AVENGINE_DATA_ROOT:-/data}"
PROFILE_ROOT="${AVENGINE_LIVE_PROFILE_ROOT:-${DATA_ROOT}/profiling/live}"
DURATION="${1:-30}"
TARGET="${2:-all}"
BACKEND="${AVENGINE_LIVE_PROFILE_BACKEND:-auto}"
SAMPLE_HZ="${AVENGINE_LIVE_PROFILE_HZ:-99}"
PROGRESS_INTERVAL="${AVENGINE_LIVE_PROFILE_PROGRESS_SECONDS:-10}"
SAMPLE_INTERVAL="${AVENGINE_LIVE_PROFILE_PROCESS_SAMPLE_SECONDS:-2}"

usage() {
  cat <<'EOF'
usage: av-live-profile [seconds] [crawler|search|all]

Collect a live runtime profile for running AVEngine processes.

Captures:
  bpftrace  on-CPU stacks (flamegraph folded output), off-CPU blocking time
            (sched_switch histograms), syscall latency histograms for
            read / write / connect / futex.
  dtrace    on-CPU stacks, syscall latency quantize aggregations.

Environment:
  AVENGINE_LIVE_PROFILE_BACKEND=auto|bpftrace|dtrace
  AVENGINE_LIVE_PROFILE_HZ=99
  AVENGINE_LIVE_PROFILE_PROGRESS_SECONDS=10
  AVENGINE_LIVE_PROFILE_PROCESS_SAMPLE_SECONDS=2
  AVENGINE_LIVE_PROFILE_ROOT=/data/profiling/live
EOF
}

if [[ "${DURATION}" == "-h" || "${DURATION}" == "--help" ]]; then
  usage
  exit 0
fi

if ! [[ "${DURATION}" =~ ^[0-9]+$ ]] || (( DURATION < 1 )); then
  echo "seconds must be a positive integer" >&2
  usage >&2
  exit 2
fi

if ! [[ "${SAMPLE_HZ}" =~ ^[0-9]+$ ]] || (( SAMPLE_HZ < 1 )); then
  echo "AVENGINE_LIVE_PROFILE_HZ must be a positive integer" >&2
  exit 2
fi

if ! [[ "${PROGRESS_INTERVAL}" =~ ^[0-9]+$ ]] || (( PROGRESS_INTERVAL < 1 )); then
  echo "AVENGINE_LIVE_PROFILE_PROGRESS_SECONDS must be a positive integer" >&2
  exit 2
fi

if ! [[ "${SAMPLE_INTERVAL}" =~ ^[0-9]+$ ]] || (( SAMPLE_INTERVAL < 1 )); then
  echo "AVENGINE_LIVE_PROFILE_PROCESS_SAMPLE_SECONDS must be a positive integer" >&2
  exit 2
fi

case "${TARGET}" in
  crawler|search|all) ;;
  *)
    echo "target must be one of: crawler, search, all" >&2
    usage >&2
    exit 2
    ;;
esac

case "${BACKEND}" in
  auto|bpftrace|dtrace) ;;
  *)
    echo "AVENGINE_LIVE_PROFILE_BACKEND must be one of: auto, bpftrace, dtrace" >&2
    exit 2
    ;;
esac

if (( EUID != 0 )); then
  if command -v sudo >/dev/null 2>&1; then
    exec sudo -n /avengine/bin/avengine-live-profile.sh "$@"
  fi
  echo "av-live-profile requires root privileges for kernel tracing" >&2
  exit 1
fi

declare -a PIDS=()
declare -a PID_LABELS=()

add_pid_file() {
  local label="$1"
  local pid_file="$2"
  local pid

  [[ -f "${pid_file}" ]] || return 0
  pid="$(tr -d '[:space:]' < "${pid_file}")"
  [[ "${pid}" =~ ^[0-9]+$ ]] || return 0
  if kill -0 "${pid}" 2>/dev/null; then
    PIDS+=("${pid}")
    PID_LABELS+=("${label}:${pid}")
  fi
}

collect_pids() {
  shopt -s nullglob
  case "${TARGET}" in
    crawler)
      for pid_file in "${DATA_ROOT}"/pids/crawler-*.pid; do
        add_pid_file "$(basename "${pid_file}" .pid)" "${pid_file}"
      done
      ;;
    search)
      add_pid_file "search-server" "${DATA_ROOT}/pids/search-server.pid"
      ;;
    all)
      for pid_file in "${DATA_ROOT}"/pids/*.pid; do
        add_pid_file "$(basename "${pid_file}" .pid)" "${pid_file}"
      done
      ;;
  esac
  shopt -u nullglob
}

join_by_comma() {
  local IFS=,
  echo "$*"
}

# Build comma-separated pid list for dtrace predicate: pid == 1 || pid == 2
build_pid_predicate() {
  local predicate=""
  local pid
  for pid in "${PIDS[@]}"; do
    if [[ -n "${predicate}" ]]; then
      predicate+=" || "
    fi
    predicate+="pid == ${pid}"
  done
  printf '%s\n' "${predicate}"
}

# Build bpftrace BEGIN-block statements to populate a @pids membership map
build_pids_init() {
  local init=""
  local pid
  for pid in "${PIDS[@]}"; do
    init+="  @pids[${pid}] = 1;"$'\n'
  done
  printf '%s' "${init}"
}

choose_backend() {
  if [[ "${BACKEND}" == "auto" ]]; then
    if command -v bpftrace >/dev/null 2>&1; then
      echo "bpftrace"
    elif command -v dtrace >/dev/null 2>&1; then
      echo "dtrace"
    else
      echo "none"
    fi
  else
    echo "${BACKEND}"
  fi
}

sample_processes() {
  local pid_csv="$1"
  local end_time
  end_time="$(($(date +%s) + DURATION))"

  while (( $(date +%s) < end_time )); do
    date -u +"%Y-%m-%dT%H:%M:%SZ"
    ps -p "${pid_csv}" -o pid,ppid,stat,pcpu,pmem,etime,comm,args || true
    echo
    sleep "${SAMPLE_INTERVAL}" || true
  done
}

collect_pids
if (( ${#PIDS[@]} == 0 )); then
  echo "no running ${TARGET} process pid files found under ${DATA_ROOT}/pids" >&2
  exit 1
fi

ACTIVE_BACKEND="$(choose_backend)"
if [[ "${ACTIVE_BACKEND}" == "none" ]]; then
  echo "neither bpftrace nor dtrace is installed in this container" >&2
  exit 1
fi
if ! command -v "${ACTIVE_BACKEND}" >/dev/null 2>&1; then
  echo "${ACTIVE_BACKEND} is not installed in this container" >&2
  exit 1
fi

RUN_ID="$(date -u +%Y%m%dT%H%M%SZ)"
OUTPUT_DIR="${PROFILE_ROOT}/${RUN_ID}-${TARGET}-${ACTIVE_BACKEND}"
mkdir -p "${OUTPUT_DIR}"

PID_CSV="$(join_by_comma "${PIDS[@]}")"
PREDICATE="$(build_pid_predicate)"
PIDS_INIT="$(build_pids_init)"
SUMMARY="${OUTPUT_DIR}/summary.md"
PROCESS_SAMPLES="${OUTPUT_DIR}/process-samples.txt"
TRACE_STATUS=0
SAMPLER_PID=""

cleanup() {
  if [[ -n "${SAMPLER_PID}" ]]; then
    kill "${SAMPLER_PID}" 2>/dev/null || true
    wait "${SAMPLER_PID}" 2>/dev/null || true
  fi
}
trap cleanup EXIT

{
  echo "# AVEngine Live Profile"
  echo
  echo "- started_utc: ${RUN_ID}"
  echo "- duration_seconds: ${DURATION}"
  echo "- target: ${TARGET}"
  echo "- backend: ${ACTIVE_BACKEND}"
  echo "- pids: ${PID_LABELS[*]}"
  echo "- sample_hz: ${SAMPLE_HZ}"
  echo "- output_dir: ${OUTPUT_DIR}"
} > "${SUMMARY}"

{
  echo "AVEngine live profile"
  echo "  output:   ${OUTPUT_DIR}"
  echo "  backend:  ${ACTIVE_BACKEND}"
  echo "  target:   ${TARGET}"
  echo "  pids:     ${PID_LABELS[*]}"
  echo "  duration: ${DURATION}s"
  echo
} | tee "${OUTPUT_DIR}/meta.txt"

ps -eLo pid,tid,ppid,stat,pcpu,pmem,comm,wchan:24,args > "${OUTPUT_DIR}/threads-before.txt" || true
sample_processes "${PID_CSV}" > "${PROCESS_SAMPLES}" 2>&1 &
SAMPLER_PID="$!"

# ── bpftrace ──────────────────────────────────────────────────────────────────
if [[ "${ACTIVE_BACKEND}" == "bpftrace" ]]; then
  TRACE_SCRIPT="${OUTPUT_DIR}/live-profile.bt"
  TRACE_OUT="${OUTPUT_DIR}/bpftrace.out"
  FOLDED_OUT="${OUTPUT_DIR}/on-cpu.folded"

  cat > "${TRACE_SCRIPT}" <<EOF
BEGIN
{
${PIDS_INIT}
  printf("AVEngine live profile — target: ${TARGET}, duration: ${DURATION}s\\n");
  printf("  on-CPU: ${SAMPLE_HZ}Hz sample  |  off-CPU: sched_switch  |  syscalls: read/write/connect/futex\\n\\n");
}

/* ── On-CPU sampling ─────────────────────────────────────────────── */
profile:hz:${SAMPLE_HZ}
/@pids[pid]/
{
  @on_cpu_samples[comm] = count();
  @on_cpu[comm, ustack(20)] = count();
}

/* ── Off-CPU: record when a tracked thread leaves the CPU ─────────── */
tracepoint:sched:sched_switch
/@pids[args->prev_pid]/
{
  @off_ts[args->prev_pid] = nsecs;
}

/* ── Off-CPU: accumulate blocking time when the thread returns ─────── */
tracepoint:sched:sched_switch
/@pids[args->next_pid] && @off_ts[args->next_pid]/
{
  @off_cpu_us = hist((nsecs - @off_ts[args->next_pid]) / 1000);
  @off_cpu_total_ms[str(args->next_comm)] =
      sum((nsecs - @off_ts[args->next_pid]) / 1000000);
  delete(@off_ts[args->next_pid]);
}

/* ── Syscall latency ─────────────────────────────────────────────── */
tracepoint:syscalls:sys_enter_read    /@pids[pid]/ { @sys_ts[tid, "read"]    = nsecs; }
tracepoint:syscalls:sys_enter_write   /@pids[pid]/ { @sys_ts[tid, "write"]   = nsecs; }
tracepoint:syscalls:sys_enter_connect /@pids[pid]/ { @sys_ts[tid, "connect"] = nsecs; }
tracepoint:syscalls:sys_enter_futex   /@pids[pid]/ { @sys_ts[tid, "futex"]   = nsecs; }

tracepoint:syscalls:sys_exit_read
/@sys_ts[tid, "read"]/
{
  @syscall_us["read"] = hist((nsecs - @sys_ts[tid, "read"]) / 1000);
  delete(@sys_ts[tid, "read"]);
}
tracepoint:syscalls:sys_exit_write
/@sys_ts[tid, "write"]/
{
  @syscall_us["write"] = hist((nsecs - @sys_ts[tid, "write"]) / 1000);
  delete(@sys_ts[tid, "write"]);
}
tracepoint:syscalls:sys_exit_connect
/@sys_ts[tid, "connect"]/
{
  @syscall_us["connect"] = hist((nsecs - @sys_ts[tid, "connect"]) / 1000);
  delete(@sys_ts[tid, "connect"]);
}
tracepoint:syscalls:sys_exit_futex
/@sys_ts[tid, "futex"]/
{
  @syscall_us["futex"] = hist((nsecs - @sys_ts[tid, "futex"]) / 1000);
  delete(@sys_ts[tid, "futex"]);
}

/* ── Progress summary ────────────────────────────────────────────── */
interval:s:${PROGRESS_INTERVAL}
{
  printf("\\n── %lld s elapsed ──\\n", elapsed / 1000000000);
  printf("on-CPU samples:\\n");    print(@on_cpu_samples);
  printf("off-CPU total ms:\\n");  print(@off_cpu_total_ms);
}

interval:s:${DURATION}
{
  exit();
}

END
{
  printf("\\n════════════════════════════════════════\\n");
  printf("  ON-CPU STACKS  (comm × stack → sample count)\\n");
  printf("════════════════════════════════════════\\n");
  print(@on_cpu);

  printf("\\n════════════════════════════════════════\\n");
  printf("  OFF-CPU HISTOGRAM  (blocking time in µs)\\n");
  printf("════════════════════════════════════════\\n");
  print(@off_cpu_us);

  printf("\\n════════════════════════════════════════\\n");
  printf("  OFF-CPU TOTAL BY THREAD  (ms blocked)\\n");
  printf("════════════════════════════════════════\\n");
  print(@off_cpu_total_ms);

  printf("\\n════════════════════════════════════════\\n");
  printf("  SYSCALL LATENCY HISTOGRAMS  (µs)\\n");
  printf("════════════════════════════════════════\\n");
  print(@syscall_us);

  clear(@pids); clear(@off_ts); clear(@sys_ts);
}
EOF

  if timeout "$((DURATION + 15))s" bpftrace "${TRACE_SCRIPT}" 2>&1 | tee "${TRACE_OUT}"; then
    TRACE_STATUS=0
  else
    TRACE_STATUS=$?
  fi

  # Post-process: convert @on_cpu stacks to flamegraph folded format
  echo
  echo "Generating flamegraph input: ${FOLDED_OUT}"
  python3 - "${TRACE_OUT}" "${FOLDED_OUT}" <<'PYEOF'
import sys, re, pathlib

src = pathlib.Path(sys.argv[1]).read_text(errors="replace")
out_path = pathlib.Path(sys.argv[2])
entries = []

# bpftrace prints 2-key stack maps as:
#   @mapname[label,
#           frame3+0xNN
#           frame2+0xNN
#           frame1+0xNN
#   ]: count
pattern = re.compile(
    r'@on_cpu\[([^\n,\[]+),\s*\n((?:[ \t]+[^\n]+\n)+)\]:\s*(\d+)',
    re.MULTILINE,
)
for m in pattern.finditer(src):
    comm   = m.group(1).strip().strip('"')
    frames = []
    for line in m.group(2).splitlines():
        f = line.strip()
        if not f:
            continue
        f = re.sub(r'\+0x[0-9a-f]+', '', f)   # strip hex offsets
        f = re.sub(r'\s*\[.*?\]$', '', f)       # strip [module] annotations
        f = f.strip()
        if f and f not in ('[unknown]', ''):
            frames.append(f)
    if frames:
        frames.reverse()   # outermost frame first (flamegraph convention)
        entries.append(f"{comm};{';'.join(frames)} {m.group(3)}")

if entries:
    out_path.write_text('\n'.join(entries) + '\n', encoding='utf-8')
    print(f"  {len(entries)} stack entries → {out_path}")
    print(f"  render: flamegraph.pl {out_path} > flame.svg")
else:
    print("  (no parseable on-CPU stack entries found in trace output)")
PYEOF

# ── dtrace ────────────────────────────────────────────────────────────────────
else
  TRACE_SCRIPT="${OUTPUT_DIR}/live-profile.d"
  TRACE_OUT="${OUTPUT_DIR}/dtrace.out"

  cat > "${TRACE_SCRIPT}" <<EOF
/* ── On-CPU sampling ─────────────────────────────────────────────── */
profile-997
/${PREDICATE}/
{
  @on_cpu[execname, ustack(20)] = count();
  @on_cpu_samples[execname]     = count();
}

/* ── Syscall latency ─────────────────────────────────────────────── */
syscall::read:entry,
syscall::write:entry,
syscall::connect:entry,
syscall::futex:entry
/${PREDICATE}/
{
  self->syscall_ts   = timestamp;
  self->syscall_name = probefunc;
}

syscall::read:return,
syscall::write:return,
syscall::connect:return,
syscall::futex:return
/self->syscall_ts/
{
  @syscall_ns[self->syscall_name] = quantize(timestamp - self->syscall_ts);
  self->syscall_ts   = 0;
  self->syscall_name = "";
}

/* ── Progress ────────────────────────────────────────────────────── */
tick-${PROGRESS_INTERVAL}s
{
  printf("\\n── progress ──\\n");
  printa("  %s: %@d cpu samples\\n", @on_cpu_samples);
}

/* ── Final report ────────────────────────────────────────────────── */
tick-${DURATION}s
{
  printf("\\n=== ON-CPU STACKS ===\\n");
  printa(@on_cpu);
  printf("\\n=== SYSCALL LATENCY (ns) ===\\n");
  printa(@syscall_ns);
  exit(0);
}
EOF

  if timeout "$((DURATION + 15))s" dtrace -q -s "${TRACE_SCRIPT}" 2>&1 | tee "${TRACE_OUT}"; then
    TRACE_STATUS=0
  else
    TRACE_STATUS=$?
  fi
fi

ps -eLo pid,tid,ppid,stat,pcpu,pmem,comm,wchan:24,args > "${OUTPUT_DIR}/threads-after.txt" || true

{
  echo
  echo "## Result"
  echo
  echo "- trace_status: ${TRACE_STATUS}"
  echo "- trace_output: $(basename "${TRACE_OUT}")"
  echo "- process_samples: $(basename "${PROCESS_SAMPLES}")"
  echo "- threads_before: threads-before.txt"
  echo "- threads_after: threads-after.txt"
} >> "${SUMMARY}"

echo
if (( TRACE_STATUS == 0 )); then
  echo "Live profile complete: ${OUTPUT_DIR}"
else
  echo "Live profile finished with status ${TRACE_STATUS}: ${OUTPUT_DIR}" >&2
fi

exit "${TRACE_STATUS}"
