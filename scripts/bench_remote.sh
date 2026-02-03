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
WORKERS="${WORKERS:-8}"
EXPECTED_SPAWNS="${EXPECTED_SPAWNS:-0}"
REMOTE_DIR="${REMOTE_DIR:-$HOME/bench}"
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

ssh "${REMOTE}" "mkdir -p '${REMOTE_RUN_DIR}'"

# Prepare execution binaries: either upload local bins or build on remote
REMOTE_BIN_DIR="${REMOTE_RUN_DIR}"
if [[ "${REMOTE_BUILD}" == "1" ]]; then
  echo "REMOTE_BUILD=1: rsyncing source and building on remote..." >&2
  # rsync source tree (exclude build and VCS noise)
  rsync -az --delete \
    --exclude 'build/' \
    --exclude '.git/' \
    --exclude '.vscode/' \
    --exclude '*.o' \
    --exclude '*.a' \
    ./ "${REMOTE}:${REMOTE_RUN_DIR}/srcdir/"
  # build remotely
  ssh "${REMOTE}" bash -lc "
set -euo pipefail
cd '${REMOTE_RUN_DIR}/srcdir'
if ! command -v make >/dev/null 2>&1; then echo 'ERROR: make missing on remote' >&2; exit 2; fi
make p008_build
"
  REMOTE_BIN_DIR="${REMOTE_RUN_DIR}/srcdir/build"
else
  # Verify local binaries and upload
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

# Start server and CPU monitors on remote
echo "Starting server and CPU monitors on remote..." >&2
ssh "${REMOTE}" bash -lc "
set -euo pipefail
cd '${REMOTE_RUN_DIR}'
# Raise fd limits just in case
ulimit -n 65536 || true
# Ensure CPU tools present (auto-install if missing)
if ! command -v mpstat >/dev/null 2>&1 || ! command -v pidstat >/dev/null 2>&1; then
  if command -v apt-get >/dev/null 2>&1; then
    (apt-get update && apt-get install -y sysstat) >/dev/null 2>&1 || echo 'WARN: sysstat install failed (apt-get)' >> warn.log
  elif command -v yum >/dev/null 2>&1; then
    (yum install -y sysstat) >/dev/null 2>&1 || echo 'WARN: sysstat install failed (yum)' >> warn.log
  elif command -v dnf >/dev/null 2>&1; then
    (dnf install -y sysstat) >/dev/null 2>&1 || echo 'WARN: sysstat install failed (dnf)' >> warn.log
  else
    echo 'WARN: cannot auto-install sysstat; no known package manager' >> warn.log
  fi
fi
# Start server (duration_ms controls shutdown)
nohup '${REMOTE_BIN_DIR}/p008_net_repl_server' ${PORT} ${CLIENTS} ${DURATION_MS} ${TICK_HZ} ${WORKERS} > server.out 2>&1 & echo \$! > server.pid
sleep 1
SERVER_PID=\$(cat server.pid)
# Start per-CPU usage logging (mpstat)
if command -v mpstat >/dev/null 2>&1; then
  nohup mpstat -P ALL 1 > cpu.mpstat 2>&1 & echo \$! > mpstat.pid
else
  echo 'WARN: mpstat not found; skipping per-CPU logs' >> warn.log
fi
# Start server process CPU logging (pidstat)
if command -v pidstat >/dev/null 2>&1; then
  nohup pidstat -u -p \$SERVER_PID 1 > cpu.pidstat 2>&1 & echo \$! > pidstat.pid
else
  echo 'WARN: pidstat not found; skipping per-process CPU logs' >> warn.log
fi
# Wait for readiness
READY_WAIT_MS=5000
READY_START=\$(date +%s%3N)
while true; do
  if grep -q 'P008_REPL_SERVER_READY' server.out 2>/dev/null; then break; fi
  NOW=\$(date +%s%3N)
  if (( NOW - READY_START > READY_WAIT_MS )); then echo 'WARN: server did not report ready within timeout' >> warn.log; break; fi
  sleep 0.1
done
"

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
  ssh "${REMOTE}" bash -lc "
set -euo pipefail
cd '${REMOTE_RUN_DIR}'
: > clients.out
for i in \$(seq 1 ${CLIENTS}); do
  nohup '${REMOTE_BIN_DIR}/p008_net_repl_client' 127.0.0.1 ${PORT} ${CLIENT_DURATION} ${EXPECTED_SPAWNS} ${TICK_HZ} >> clients.out 2>&1 &
done
# Wait until all background jobs finish
wait || true
"
fi

# Wait for server to exit (duration-based), then stop monitors
echo "Waiting for server to finish..." >&2
ssh "${REMOTE}" bash -lc "
set -euo pipefail
cd '${REMOTE_RUN_DIR}'
# Poll server pid until exit
while kill -0 \$(cat server.pid) 2>/dev/null; do sleep 1; done
# Stop CPU monitors if present
if [[ -f mpstat.pid ]]; then kill \$(cat mpstat.pid) 2>/dev/null || true; fi
if [[ -f pidstat.pid ]]; then kill \$(cat pidstat.pid) 2>/dev/null || true; fi
"

# Fetch logs
LOCAL_LOG_DIR="${ROOT_DIR}/bench-logs/${RUN_TAG}"
mkdir -p "${LOCAL_LOG_DIR}"
echo "Fetching server logs to ${LOCAL_LOG_DIR}..." >&2
scp "${REMOTE}:${REMOTE_RUN_DIR}/server.out" "${LOCAL_LOG_DIR}/"
if [[ "${CLIENTS_LOCAL}" != "1" ]]; then
  scp "${REMOTE}:${REMOTE_RUN_DIR}/clients.out" "${LOCAL_LOG_DIR}/"
fi
scp "${REMOTE}:${REMOTE_RUN_DIR}/cpu.mpstat" "${LOCAL_LOG_DIR}/" || true
scp "${REMOTE}:${REMOTE_RUN_DIR}/cpu.pidstat" "${LOCAL_LOG_DIR}/" || true
scp "${REMOTE}:${REMOTE_RUN_DIR}/warn.log" "${LOCAL_LOG_DIR}/" || true

# Quick summary
echo "--- Summary (${RUN_TAG}) ---"
if grep -q '^p008 stats:' "${LOCAL_LOG_DIR}/server.out"; then
  grep '^p008 stats:' "${LOCAL_LOG_DIR}/server.out" | tail -n 1
else
  echo "Server stats not found in server.out" >&2
fi
CLIENT_STAT_FILE="${LOCAL_LOG_DIR}/clients.out"
CLIENT_STAT_LINES=$(grep -c '^P008_CLIENT_STATS' "${CLIENT_STAT_FILE}" || true)
echo "Clients completed: ${CLIENT_STAT_LINES}/${CLIENTS}"
# Show average client rx/tx mbps if available
if [[ "${CLIENT_STAT_LINES}" -gt 0 ]]; then
  awk '/^P008_CLIENT_STATS/ {tx+=substr($0, index($0, "tx_mbps=")+8); rx+=substr($0, index($0, "rx_mbps=")+8)} END {if(NR>0){printf "Avg tx_mbps=%.3f rx_mbps=%.3f\n", tx/NR, rx/NR}}' "${CLIENT_STAT_FILE}" || true
fi

echo "Logs: ${LOCAL_LOG_DIR}"
