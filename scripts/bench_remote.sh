#!/usr/bin/env bash
set -euo pipefail

# Remote UDP replication benchmark runner
# - Two modes:
#   (A) Upload local binaries and run on remote (default)
#   (B) REMOTE_BUILD=1: rsync source, build on remote, then run (for arch mismatch)
# - Client location:
#   CLIENTS_LOCAL=1: spawn clients locally; otherwise on remote
# - Runs server with configured workers and tick rate
# - Spawns N clients (default 64)
# - Logs CPU usage (mpstat per-CPU; pidstat for server PID)
# - Captures server and client throughput/stats
# - Fetches logs back to local bench-logs/ directory
#
# Requirements on remote:
# - bash, ssh, scp
# - mpstat (sysstat) and pidstat (sysstat) recommended
# - UDP port open (default 40001)
#
# Usage:
#   REMOTE=user@host ./scripts/bench_remote.sh [options]
#
# Env options:
#   REMOTE           SSH target in form user@host (required if not first arg)
#   PORT             UDP port (default: 40001)
#   CLIENTS          Number of clients (default: 64)
#   DURATION_MS      Server run duration in ms (default: 20000)
#   CLIENT_DURATION  Client duration in ms (default: DURATION_MS-500)
#   TICK_HZ          Server tick Hz (default: 60)
#   WORKERS          Server worker threads (default: 8)
#   EXPECTED_SPAWNS  Client expected spawns (default: 0 to accept WELCOME)
#   REMOTE_DIR       Remote working dir (default: ~/bench)
#   LOG_DIR          Local logs dir (default: bench-logs/<timestamp>_port_clients)
#   REMOTE_BUILD     If set to 1, rsync source and build on remote (default: 0)
#   CLIENTS_LOCAL    If set to 1, run clients locally, targeting SERVER_IPV4 (default: 0)
#   SERVER_IPV4      Server IPv4 (default: derived from REMOTE host after @)
#
# Example:
#   REMOTE=bench@10.0.0.5 PORT=40001 CLIENTS=64 DURATION_MS=30000 \
#   TICK_HZ=60 WORKERS=8 ./scripts/bench_remote.sh

# Optional sweep: if SWEEP_MIN and SWEEP_MAX are set, run multiple CLIENTS values sequentially.
# Perform rsync/build ONCE, then reuse the built binaries for each run.
if [[ -n "${SWEEP_MIN:-}" && -n "${SWEEP_MAX:-}" && -z "${__P008_SWEEPING:-}" ]]; then
  export __P008_SWEEPING=1
  # Resolve REMOTE early
  REMOTE_SWEEP="${REMOTE:-${1:-}}"
  if [[ -z "${REMOTE_SWEEP}" ]]; then
    echo "ERROR: REMOTE not provided for sweep. Set REMOTE=user@host or pass as first arg." >&2
    exit 2
  fi
  # Build once into a fixed remote path under $HOME to avoid re-rsync per run
  SWEEP_BUILD_REL="bench/p008_sweep_build"
  ssh "${REMOTE_SWEEP}" "bash -lc 'mkdir -p \"\$HOME/${SWEEP_BUILD_REL}/srcdir\"'"
  REMOTE_BIN_DIR_OVERRIDE=""
  if [[ "${REMOTE_BUILD:-1}" == "1" ]]; then
    echo "Sweep: rsyncing source and building once on remote at ~/${SWEEP_BUILD_REL}/srcdir..." >&2
    rsync -az --delete \
      --exclude 'build/' \
      --exclude '.git/' \
      --exclude '.vscode/' \
      --exclude '*.o' \
      --exclude '*.a' \
      ./ "${REMOTE_SWEEP}:~/${SWEEP_BUILD_REL}/srcdir/"
    ssh "${REMOTE_SWEEP}" "bash -lc 'set -euo pipefail; cd \"\$HOME/${SWEEP_BUILD_REL}/srcdir\"; if ! command -v make >/dev/null 2>&1; then echo \"ERROR: make missing on remote\" >&2; exit 2; fi; make p008_build'"
  fi
  # Run each client-count sequentially, reusing built binaries
  for ci in $(seq "${SWEEP_MIN}" "${SWEEP_MAX}"); do
    CLIENTS="${ci}" SWEEP_MIN= SWEEP_MAX= __P008_SWEEPING=1 \
      REMOTE="${REMOTE_SWEEP}" PORT="${PORT:-40001}" \
      DURATION_MS="${DURATION_MS:-20000}" TICK_HZ="${TICK_HZ:-60}" \
      WORKERS="${WORKERS:-1}" EXPECTED_SPAWNS="${EXPECTED_SPAWNS:-0}" \
      REMOTE_DIR="${REMOTE_DIR:-~/bench}" REMOTE_BUILD=0 \
      CLIENTS_LOCAL="${CLIENTS_LOCAL:-1}" SERVER_IPV4="${SERVER_IPV4:-}" \
      USE_SWEEP_BIN=1 \
      bash "${BASH_SOURCE[0]}"
  done
  exit 0
fi

REMOTE="${REMOTE:-}"
if [[ -z "${REMOTE}" ]]; then
  REMOTE="${1:-}"
fi
if [[ -z "${REMOTE}" ]]; then
  echo "ERROR: REMOTE not provided. Set REMOTE=user@host or pass as first arg." >&2
  exit 2
fi

PORT="${PORT:-40001}"
CLIENTS="${CLIENTS:-64}"
DURATION_MS="${DURATION_MS:-20000}"
CLIENT_DURATION="${CLIENT_DURATION:-}"  # allow override
TICK_HZ="${TICK_HZ:-60}"
WORKERS="${WORKERS:-1}"
EXPECTED_SPAWNS="${EXPECTED_SPAWNS:-0}"
REMOTE_DIR="${REMOTE_DIR:-~/bench}"
REMOTE_BUILD="${REMOTE_BUILD:-0}"
CLIENTS_LOCAL="${CLIENTS_LOCAL:-0}"

# Derive server IPv4 from REMOTE if not provided
SERVER_IPV4="${SERVER_IPV4:-}"
if [[ -z "${SERVER_IPV4}" ]]; then
  # Extract host part after @
  SERVER_IPV4="${REMOTE#*@}"
fi

# Compute default client duration if not overridden
if [[ -z "${CLIENT_DURATION}" ]]; then
  # bash arithmetic requires integers
  CLIENT_DURATION=$(( DURATION_MS > 500 ? DURATION_MS - 500 : DURATION_MS ))
fi

ROOT_DIR=$(pwd)
BUILD_DIR="${ROOT_DIR}/build"
SERVER_BIN_LOCAL="${BUILD_DIR}/p008_net_repl_server"
CLIENT_BIN_LOCAL="${BUILD_DIR}/p008_net_repl_client"

TS=$(date +%Y%m%d_%H%M%S)
RUN_TAG="p008_${TS}_port${PORT}_${CLIENTS}clients"
REMOTE_RUN_DIR="${REMOTE_DIR}/${RUN_TAG}"

echo "Remote run dir: ${REMOTE_RUN_DIR}" >&2

ssh "${REMOTE}" "mkdir -p ${REMOTE_RUN_DIR}"

# Prepare execution binaries: reuse sweep build if requested; otherwise upload or build per run
if [[ "${USE_SWEEP_BIN:-}" == "1" ]]; then
  : # No binary prep; remote will use "$HOME/${REMOTE_BIN_DIR_BASE_REL}" for server/client binaries
else
  REMOTE_BIN_DIR="${REMOTE_RUN_DIR}"
  if [[ "${REMOTE_BUILD}" == "1" ]]; then
    echo "REMOTE_BUILD=1: rsyncing source and building on remote..." >&2
    rsync -az --delete \
      --exclude 'build/' \
      --exclude '.git/' \
      --exclude '.vscode/' \
      --exclude '*.o' \
      --exclude '*.a' \
      ./ "${REMOTE}:${REMOTE_RUN_DIR}/srcdir/"
    ssh "${REMOTE}" env REMOTE_RUN_DIR="${REMOTE_RUN_DIR}" bash -lc 'set -euo pipefail; cd $REMOTE_RUN_DIR/srcdir; if ! command -v make >/dev/null 2>&1; then echo "ERROR: make missing on remote" >&2; exit 2; fi; make p008_build'
    REMOTE_BIN_DIR="${REMOTE_RUN_DIR}/srcdir/build"
  else
    if [[ ! -x "${SERVER_BIN_LOCAL}" ]] || [[ ! -x "${CLIENT_BIN_LOCAL}" ]]; then
      echo "Building p008 binaries locally..." >&2
      make p008_build
    fi
    if [[ ! -x "${SERVER_BIN_LOCAL}" ]] || [[ ! -x "${CLIENT_BIN_LOCAL}" ]]; then
      echo "ERROR: Required binaries missing in ./build (p008_net_repl_server, p008_net_repl_client)." >&2
      exit 1
    fi
    echo "Uploading binaries to ${REMOTE}:${REMOTE_RUN_DIR}..." >&2
    scp "${SERVER_BIN_LOCAL}" "${CLIENT_BIN_LOCAL}" "${REMOTE}:${REMOTE_RUN_DIR}/"
  fi
fi

# Start server and CPU monitors on remote
echo "Starting server and CPU monitors on remote..." >&2
ssh "${REMOTE}" env REMOTE_RUN_DIR="${REMOTE_RUN_DIR}" REMOTE_BIN_DIR="${REMOTE_BIN_DIR:-}" USE_SWEEP_BIN="${USE_SWEEP_BIN:-}" PORT="${PORT}" CLIENTS="${CLIENTS}" DURATION_MS="${DURATION_MS}" TICK_HZ="${TICK_HZ}" WORKERS="${WORKERS}" bash -lc 'set -euo pipefail; cd $REMOTE_RUN_DIR; ulimit -n 65536 || true; if ! command -v mpstat >/dev/null 2>&1 || ! command -v pidstat >/dev/null 2>&1; then if command -v apt-get >/dev/null 2>&1; then (apt-get update && apt-get install -y sysstat) >/dev/null 2>&1 || echo "WARN: sysstat install failed (apt-get)" >> warn.log; elif command -v yum >/dev/null 2>&1; then (yum install -y sysstat) >/dev/null 2>&1 || echo "WARN: sysstat install failed (yum)" >> warn.log; elif command -v dnf >/dev/null 2>&1; then (dnf install -y sysstat) >/dev/null 2>&1 || echo "WARN: sysstat install failed (dnf)" >> warn.log; else echo "WARN: cannot auto-install sysstat; no known package manager" >> warn.log; fi; fi; if [[ -n "$USE_SWEEP_BIN" ]]; then REMOTE_BIN_DIR="$HOME/bench/p008_sweep_build/srcdir/build"; fi; nohup "$REMOTE_BIN_DIR/p008_net_repl_server" "$PORT" "$CLIENTS" "$DURATION_MS" "$TICK_HZ" "$WORKERS" > server.out 2>&1 & echo $! > server.pid; sleep 1; SERVER_PID=$(cat server.pid); if command -v mpstat >/dev/null 2>&1; then nohup mpstat -P ALL 1 > cpu.mpstat 2>&1 & echo $! > mpstat.pid; else echo "WARN: mpstat not found; skipping per-CPU logs" >> warn.log; fi; if command -v pidstat >/dev/null 2>&1; then nohup pidstat -u -p $SERVER_PID 1 > cpu.pidstat 2>&1 & echo $! > pidstat.pid; else echo "WARN: pidstat not found; skipping per-process CPU logs" >> warn.log; fi; READY_WAIT_MS=5000; READY_START=$(date +%s%3N); while true; do if grep -q "P008_REPL_SERVER_READY" server.out 2>/dev/null; then break; fi; NOW=$(date +%s%3N); if (( NOW - READY_START > READY_WAIT_MS )); then echo "WARN: server did not report ready within timeout" >> warn.log; break; fi; sleep 0.1; done'

if [[ "${CLIENTS_LOCAL}" == "1" ]]; then
  echo "Spawning ${CLIENTS} clients locally against ${SERVER_IPV4}:${PORT}..." >&2
  # Ensure local client binary exists
  if [[ ! -x "${CLIENT_BIN_LOCAL}" ]]; then
    echo "Building local client binary..." >&2
    make p008_build
  fi
  LOCAL_LOG_DIR="${ROOT_DIR}/bench-logs/${RUN_TAG}"
  mkdir -p "${LOCAL_LOG_DIR}"
  : > "${LOCAL_LOG_DIR}/clients.out"
  for i in $(seq 1 ${CLIENTS}); do
    nohup "${CLIENT_BIN_LOCAL}" "${SERVER_IPV4}" ${PORT} ${CLIENT_DURATION} ${EXPECTED_SPAWNS} ${TICK_HZ} >> "${LOCAL_LOG_DIR}/clients.out" 2>&1 &
  done
  # Wait for local background jobs to finish
  wait || true
else
  # Start clients on remote (loopback to server)
  echo "Spawning ${CLIENTS} clients on remote..." >&2
  ssh "${REMOTE}" env REMOTE_RUN_DIR="${REMOTE_RUN_DIR}" REMOTE_BIN_DIR="${REMOTE_BIN_DIR:-}" USE_SWEEP_BIN="${USE_SWEEP_BIN:-}" PORT="${PORT}" CLIENTS="${CLIENTS}" CLIENT_DURATION="${CLIENT_DURATION}" EXPECTED_SPAWNS="${EXPECTED_SPAWNS}" TICK_HZ="${TICK_HZ}" bash -lc 'set -euo pipefail; cd $REMOTE_RUN_DIR; : > clients.out; if [[ -n "$USE_SWEEP_BIN" ]]; then REMOTE_BIN_DIR="$HOME/bench/p008_sweep_build/srcdir/build"; fi; for i in $(seq 1 "$CLIENTS"); do nohup "$REMOTE_BIN_DIR/p008_net_repl_client" 127.0.0.1 "$PORT" "$CLIENT_DURATION" "$EXPECTED_SPAWNS" "$TICK_HZ" >> clients.out 2>&1 & done; wait || true'
fi

# Wait for server to exit (duration-based), then stop monitors
echo "Waiting for server to finish..." >&2
ssh "${REMOTE}" env REMOTE_RUN_DIR="${REMOTE_RUN_DIR}" bash -lc 'set -euo pipefail; cd $REMOTE_RUN_DIR; while kill -0 $(cat server.pid) 2>/dev/null; do sleep 1; done; if [[ -f mpstat.pid ]]; then kill $(cat mpstat.pid) 2>/dev/null || true; fi; if [[ -f pidstat.pid ]]; then kill $(cat pidstat.pid) 2>/dev/null || true; fi'

# Fetch logs
LOCAL_LOG_DIR="${ROOT_DIR}/bench-logs/${RUN_TAG}"
mkdir -p "${LOCAL_LOG_DIR}"
echo "Fetching server logs to ${LOCAL_LOG_DIR}..." >&2
rsync -az "${REMOTE}:${REMOTE_RUN_DIR}/server.out" "${LOCAL_LOG_DIR}/" || true
if [[ "${CLIENTS_LOCAL}" != "1" ]]; then
  rsync -az "${REMOTE}:${REMOTE_RUN_DIR}/clients.out" "${LOCAL_LOG_DIR}/" || true
fi
rsync -az "${REMOTE}:${REMOTE_RUN_DIR}/cpu.mpstat" "${LOCAL_LOG_DIR}/" || true
rsync -az "${REMOTE}:${REMOTE_RUN_DIR}/cpu.pidstat" "${LOCAL_LOG_DIR}/" || true
rsync -az "${REMOTE}:${REMOTE_RUN_DIR}/warn.log" "${LOCAL_LOG_DIR}/" || true

# Quick summary
echo "--- Summary (${RUN_TAG}) ---"
if grep -q '^p008 stats:' "${LOCAL_LOG_DIR}/server.out"; then
  SLINE=$(grep '^p008 stats:' "${LOCAL_LOG_DIR}/server.out" | tail -n 1)
  echo "$SLINE"
  # Extract state_jobs, net_io_ns, state_ns
  echo "$SLINE" | awk '{
    for (i=1; i<=NF; i++) {
      if ($i ~ /state_jobs=/) { split($i,a,"="); jobs=a[2]; }
      if ($i ~ /net_io_ns=/) { split($i,b,"="); net=b[2]; }
      if ($i ~ /state_ns=/) { split($i,c,"="); st=c[2]; }
    }
    if (jobs!="") printf("state_jobs=%s net_io_ns=%s state_ns=%s\n", jobs, net, st);
  }'
else
  echo "Server stats not found in server.out" >&2
fi
CLIENT_STAT_FILE="${LOCAL_LOG_DIR}/clients.out"
CLIENT_STAT_LINES=$(grep -c '^P008_CLIENT_STATS' "${CLIENT_STAT_FILE}" || true)
echo "Clients completed: ${CLIENT_STAT_LINES}/${CLIENTS}"
# Show average client rx/tx mbps and latency if available
if [[ "${CLIENT_STAT_LINES}" -gt 0 ]]; then
  awk '
  /^P008_CLIENT_STATS/ {
    # extract fields using regex and split
    for (i=1; i<=NF; i++) {
      if ($i ~ /tx_mbps=/) { split($i,a,"="); tx+=a[2]; }
      if ($i ~ /rx_mbps=/) { split($i,b,"="); rx+=b[2]; }
      if ($i ~ /state_inter_ms_mean=/) { split($i,c,"="); inter+=c[2]; }
      if ($i ~ /state_lag_ms_mean=/) { split($i,d,"="); lag+=d[2]; }
    }
    n++
  }
  END { if (n>0) printf "Avg tx_mbps=%.3f rx_mbps=%.3f inter_ms=%.3f lag_ms=%.3f\n", tx/n, rx/n, inter/n, lag/n }' "${CLIENT_STAT_FILE}" || true
fi

# CPU summaries
if [[ -f "${LOCAL_LOG_DIR}/cpu.mpstat" ]]; then
  awk '/all/ && $3 ~ /CPU/ {next} /all/ {idle+=$NF; n++} END {if(n>0) printf "CPU idle mean=%.2f%%\n", idle/n}' "${LOCAL_LOG_DIR}/cpu.mpstat" || true
fi
if [[ -f "${LOCAL_LOG_DIR}/cpu.pidstat" ]]; then
  awk '/Average:/ && $0 ~ /\%CPU/ {print $0} /^\s*[0-9]/ {sum+=$7; n++} END {if(n>0) printf "Server %%CPU mean=%.2f%%\n", sum/n}' "${LOCAL_LOG_DIR}/cpu.pidstat" || true
fi

echo "Logs: ${LOCAL_LOG_DIR}"
