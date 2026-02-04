#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."

# Usage: scripts/p008_perf_local_10c_180s_tracy.sh [clients] [duration_ms] [tick_hz] [workers]
CLIENTS="${1:-10}"
DURATION_MS="${2:-180000}"
TICK_HZ="${3:-60}"
WORKERS="${4:-8}"

# Run the server a little longer than clients.
SERVER_DURATION_MS=$((DURATION_MS + 2000))

TS="$(date +%Y%m%d_%H%M%S)"
RUN_LOG="build/p008_perf_run_${CLIENTS}c_${DURATION_MS}ms_${TS}.log"
SERVER_LOG="build/p008_perf_server_${CLIENTS}c_${DURATION_MS}ms_${TS}.log"
CLIENT_LOG="build/p008_perf_client_${CLIENTS}c_${DURATION_MS}ms_${TS}.log"

mkdir -p build

cleanup_procs() {
  pkill -f 'build/p008_net_perf_server_tests' 2>/dev/null || true
  pkill -f 'build/p008_net_perf_client_tests' 2>/dev/null || true
  pkill -f 'build/p008_net_repl_server' 2>/dev/null || true
  pkill -f 'build/p008_net_repl_client' 2>/dev/null || true
}

cleanup_procs

PORT="$(
  python3 - <<'PY'
import socket
s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.bind(('127.0.0.1', 0))
print(s.getsockname()[1])
s.close()
PY
)"

echo "ts=$TS port=$PORT" | tee -a "$RUN_LOG"
echo "server_log=$SERVER_LOG" | tee -a "$RUN_LOG"
echo "client_log=$CLIENT_LOG" | tee -a "$RUN_LOG"

echo "starting_server=1" | tee -a "$RUN_LOG"
stdbuf -oL -eL ./build/p008_net_perf_server_tests "$PORT" "$CLIENTS" "$SERVER_DURATION_MS" "$TICK_HZ" "$WORKERS" >"$SERVER_LOG" 2>&1 &
SERVER_PID=$!
echo "server_pid=$SERVER_PID" | tee -a "$RUN_LOG"

READY=0
for _ in $(seq 1 300); do
  if grep -q 'P008_REPL_SERVER_READY' "$SERVER_LOG"; then READY=1; break; fi
  sleep 0.1
done

if [ "$READY" -ne 1 ]; then
  echo "server_not_ready=1" | tee -a "$RUN_LOG"
  tail -n 120 "$SERVER_LOG" | tee -a "$RUN_LOG" || true
  cleanup_procs
  exit 1
fi

echo "server_ready=1" | tee -a "$RUN_LOG"
echo "starting_clients=1" | tee -a "$RUN_LOG"

set +e
stdbuf -oL -eL timeout $(((DURATION_MS / 1000) + 45))s \
  ./build/p008_net_perf_client_tests 127.0.0.1 "$PORT" "$CLIENTS" "$DURATION_MS" "$TICK_HZ" \
  >"$CLIENT_LOG" 2>&1
CLIENT_STATUS=$?
set -e

echo "client_exit=$CLIENT_STATUS" | tee -a "$RUN_LOG"

# Let server exit naturally; otherwise stop it.
for _ in $(seq 1 50); do
  if ! kill -0 "$SERVER_PID" 2>/dev/null; then break; fi
  sleep 0.1
done

if kill -0 "$SERVER_PID" 2>/dev/null; then
  echo "stopping_server=1" | tee -a "$RUN_LOG"
  kill "$SERVER_PID" 2>/dev/null || true
  wait "$SERVER_PID" 2>/dev/null || true
fi

echo "--- summaries ---" | tee -a "$RUN_LOG"
( grep -E 'P008_PERF_SUMMARY|P008_SERVER_STATS' "$CLIENT_LOG" "$SERVER_LOG" || true ) | tee -a "$RUN_LOG"

echo "--- client tail ---" | tee -a "$RUN_LOG"
tail -n 40 "$CLIENT_LOG" | tee -a "$RUN_LOG" || true

echo "--- server tail ---" | tee -a "$RUN_LOG"
tail -n 40 "$SERVER_LOG" | tee -a "$RUN_LOG" || true

echo "done=1" | tee -a "$RUN_LOG"

exit "$CLIENT_STATUS"
