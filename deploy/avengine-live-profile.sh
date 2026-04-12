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

choose_backend() {
  if [[ "${BACKEND}" == "auto" ]]; then
    if command -v dtrace >/dev/null 2>&1; then
      echo "dtrace"
    elif command -v bpftrace >/dev/null 2>&1; then
      echo "bpftrace"
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
  echo "neither dtrace nor bpftrace is installed in this container" >&2
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
  echo "  output: ${OUTPUT_DIR}"
  echo "  backend: ${ACTIVE_BACKEND}"
  echo "  target: ${TARGET}"
  echo "  pids: ${PID_LABELS[*]}"
  echo "  duration: ${DURATION}s"
  echo
} | tee "${OUTPUT_DIR}/meta.txt"

ps -eLo pid,tid,ppid,stat,pcpu,pmem,comm,wchan:24,args > "${OUTPUT_DIR}/threads-before.txt" || true
sample_processes "${PID_CSV}" > "${PROCESS_SAMPLES}" 2>&1 &
SAMPLER_PID="$!"

if [[ "${ACTIVE_BACKEND}" == "bpftrace" ]]; then
  TRACE_SCRIPT="${OUTPUT_DIR}/live-profile.bt"
  TRACE_OUT="${OUTPUT_DIR}/bpftrace.out"
  cat > "${TRACE_SCRIPT}" <<EOF
BEGIN
{
  printf("profiling AVEngine processes for ${DURATION}s\\n");
}

profile:hz:${SAMPLE_HZ}
/${PREDICATE}/
{
  @samples[comm] = count();
  @user_stacks[comm, ustack(20)] = count();
}

tracepoint:syscalls:sys_enter_*
/${PREDICATE}/
{
  @syscalls[comm, probe] = count();
}

interval:s:${PROGRESS_INTERVAL}
{
  printf("\\n--- live sample summary ---\\n");
  print(@samples, 20);
  print(@syscalls, 20);
}

interval:s:${DURATION}
{
  exit();
}
EOF

  if timeout "$((DURATION + 15))s" bpftrace "${TRACE_SCRIPT}" 2>&1 | tee "${TRACE_OUT}"; then
    TRACE_STATUS=0
  else
    TRACE_STATUS=$?
  fi
else
  TRACE_SCRIPT="${OUTPUT_DIR}/live-profile.d"
  TRACE_OUT="${OUTPUT_DIR}/dtrace.out"
  cat > "${TRACE_SCRIPT}" <<EOF
profile-997
/${PREDICATE}/
{
  @[execname, ustack()] = count();
}

tick-${DURATION}s
{
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
