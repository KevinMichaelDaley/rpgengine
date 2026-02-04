#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."

# Runs a 24-client server scenario with:
#   - Tracy enabled (`TRACY=1` build required)
#   - One OpenGL renderer client inside a network namespace
#   - tc-netem impairment applied on the veth pair
#
# Topology:
#   root ns:  veth-host0 10.200.1.1/24
#   netns:    veth-ns0   10.200.1.2/24
#
# Impairment defaults requested:
#   - 100ms latency
#   - 60ms jitter
#   - 3% packet loss
#   - 80ms reordering @ 20%   (tc-netem does not model a "reorder delay" directly; we approximate using reorder correlation)
#   - 8% burst loss           (approximated via loss correlation)
#   - 2% duplication
#
# Usage:
#   scripts/p008_render_24c_netem_tracy.sh [duration_ms] [tick_hz] [workers]
#
# Overrides (env):
#   CLIENTS_TOTAL (default 24)
#   RENDER_SEED   (default 123)
#   NETEM_ARGS    (default composed from the parameters below)
#   NS            (default netem1)
#   HOST_IP       (default 10.200.1.1)

CLIENTS_TOTAL="${CLIENTS_TOTAL:-24}"
DURATION_MS="${1:-180000}"
TICK_HZ="${2:-60}"
WORKERS="${3:-8}"
RENDER_SEED="${RENDER_SEED:-123}"

NS="${NS:-netem1}"
HOST_IP="${HOST_IP:-10.200.1.1}"

LATENCY_MS="${LATENCY_MS:-100}"
JITTER_MS="${JITTER_MS:-60}"
LOSS_PCT="${LOSS_PCT:-3}"
BURST_LOSS_CORR_PCT="${BURST_LOSS_CORR_PCT:-8}"
DUP_PCT="${DUP_PCT:-2}"
REORDER_PCT="${REORDER_PCT:-20}"
REORDER_CORR_PCT="${REORDER_CORR_PCT:-25}"
REORDER_DELAY_MS="${REORDER_DELAY_MS:-80}"

NETEM_ARGS_DEFAULT=(
  delay "${LATENCY_MS}ms" "${JITTER_MS}ms"
  loss "${LOSS_PCT}%" "${BURST_LOSS_CORR_PCT}%"
  duplicate "${DUP_PCT}%"
  reorder "${REORDER_PCT}%" "${REORDER_CORR_PCT}%"
)

# Allow raw override as a single string, e.g.
#   NETEM_ARGS='delay 120ms 40ms loss 1% duplicate 0.5% reorder 10% 25%'
NETEM_ARGS_STR="${NETEM_ARGS:-${NETEM_ARGS_DEFAULT[*]}}"

SERVER_DURATION_MS=$((DURATION_MS + 2000))
HEADLESS_CLIENTS=$((CLIENTS_TOTAL - 1))
if [ "$HEADLESS_CLIENTS" -lt 0 ]; then
  echo "error: CLIENTS_TOTAL must be >= 1" >&2
  exit 2
fi

TS="$(date +%Y%m%d_%H%M%S)"
RUN_LOG="build/p008_render_run_${CLIENTS_TOTAL}c_${DURATION_MS}ms_${TS}.log"
SERVER_LOG="build/p008_render_server_${CLIENTS_TOTAL}c_${DURATION_MS}ms_${TS}.log"
HEADLESS_LOG="build/p008_render_headless_${HEADLESS_CLIENTS}c_${DURATION_MS}ms_${TS}.log"
RENDER_LOG="build/p008_render_opengl_1c_${DURATION_MS}ms_${TS}.log"

mkdir -p build

cleanup_procs() {
  pkill -f 'build/p008_net_perf_server_tests' 2>/dev/null || true
  pkill -f 'build/p008_net_perf_client_tests' 2>/dev/null || true
  pkill -f 'build/p008_net_repl_server' 2>/dev/null || true
  pkill -f 'build/p008_net_repl_client' 2>/dev/null || true
  pkill -f 'build/p008_renderer_client' 2>/dev/null || true
}

cleanup_netem() {
  ./scripts/netem_localhost_ns.sh clear >/dev/null 2>&1 || true
  ./scripts/netem_localhost_ns.sh down >/dev/null 2>&1 || true
}

on_exit() {
  set +e
  cleanup_procs
  cleanup_netem
}
trap on_exit EXIT

cleanup_procs
cleanup_netem

# Ensure tracy-profiler is running (best-effort; won't fail the run).
if [ -f build/tracy_profiler.pid ] && kill -0 "$(cat build/tracy_profiler.pid)" 2>/dev/null; then
  echo "tracy_profiler_running=1 pid=$(cat build/tracy_profiler.pid)" | tee -a "$RUN_LOG"
else
  rm -f build/tracy_profiler.log
  (extern/tracy/profiler/build/tracy-profiler > build/tracy_profiler.log 2>&1 & echo $! > build/tracy_profiler.pid) || true
  sleep 0.5 || true
  if [ -f build/tracy_profiler.pid ] && kill -0 "$(cat build/tracy_profiler.pid)" 2>/dev/null; then
    echo "tracy_profiler_started=1 pid=$(cat build/tracy_profiler.pid)" | tee -a "$RUN_LOG"
  else
    echo "tracy_profiler_started=0" | tee -a "$RUN_LOG"
  fi
fi

# Build required binaries (must be TRACY=1).
make -j"$(nproc)" TRACY=1 \
  build/p008_net_perf_server_tests \
  build/p008_net_perf_client_tests \
  build/p008_net_repl_server \
  build/p008_net_repl_client \
  build/p008_renderer_client

PORT="$(
  python3 - <<'PY'
import socket
s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.bind(('127.0.0.1', 0))
print(s.getsockname()[1])
s.close()
PY
)"

{
  echo "ts=$TS port=$PORT"
  echo "clients_total=$CLIENTS_TOTAL headless_clients=$HEADLESS_CLIENTS"
  echo "duration_ms=$DURATION_MS tick_hz=$TICK_HZ workers=$WORKERS"
  echo "netns=$NS host_ip=$HOST_IP"
  echo "netem_args=$NETEM_ARGS_STR"
  echo "note_reorder_delay_ms=$REORDER_DELAY_MS (approx-only; tc-netem has no reorder-delay knob)"
  echo "server_log=$SERVER_LOG"
  echo "headless_log=$HEADLESS_LOG"
  echo "render_log=$RENDER_LOG"
} | tee -a "$RUN_LOG"

./scripts/netem_localhost_ns.sh up | tee -a "$RUN_LOG"
./scripts/netem_localhost_ns.sh netem $NETEM_ARGS_STR | tee -a "$RUN_LOG"

echo "starting_server=1" | tee -a "$RUN_LOG"
stdbuf -oL -eL ./build/p008_net_perf_server_tests "$PORT" "$CLIENTS_TOTAL" "$SERVER_DURATION_MS" "$TICK_HZ" "$WORKERS" >"$SERVER_LOG" 2>&1 &
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
  exit 1
fi

echo "server_ready=1" | tee -a "$RUN_LOG"

HEADLESS_STATUS=0
RENDER_STATUS=0

if [ "$HEADLESS_CLIENTS" -gt 0 ]; then
  echo "starting_headless_clients=1" | tee -a "$RUN_LOG"
  set +e
  stdbuf -oL -eL timeout $(((DURATION_MS / 1000) + 60))s \
    ./build/p008_net_perf_client_tests 127.0.0.1 "$PORT" "$HEADLESS_CLIENTS" "$DURATION_MS" "$TICK_HZ" \
    >"$HEADLESS_LOG" 2>&1 &
  HEADLESS_PID=$!
  set -e
  echo "headless_pid=$HEADLESS_PID" | tee -a "$RUN_LOG"
else
  echo "starting_headless_clients=0" | tee -a "$RUN_LOG"
fi

echo "starting_opengl_client_in_netns=1" | tee -a "$RUN_LOG"
set +e
sudo -E ip netns exec "$NS" sudo -u "$USER" -E \
  stdbuf -oL -eL ./build/p008_renderer_client "$HOST_IP" "$PORT" "$DURATION_MS" --seed "$RENDER_SEED" \
  >"$RENDER_LOG" 2>&1 &
RENDER_PID=$!
set -e

echo "render_pid=$RENDER_PID" | tee -a "$RUN_LOG"

if [ "${HEADLESS_PID:-}" != "" ]; then
  set +e
  wait "$HEADLESS_PID"
  HEADLESS_STATUS=$?
  set -e
  echo "headless_exit=$HEADLESS_STATUS" | tee -a "$RUN_LOG"
fi

set +e
wait "$RENDER_PID"
RENDER_STATUS=$?
set -e

echo "render_exit=$RENDER_STATUS" | tee -a "$RUN_LOG"

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
( grep -E 'P008_PERF_SUMMARY|P008_SERVER_STATS' "$HEADLESS_LOG" "$SERVER_LOG" 2>/dev/null || true ) | tee -a "$RUN_LOG"

echo "--- tails ---" | tee -a "$RUN_LOG"
{
  echo "--- headless tail ---"
  tail -n 40 "$HEADLESS_LOG" 2>/dev/null || true
  echo "--- render tail ---"
  tail -n 60 "$RENDER_LOG" 2>/dev/null || true
  echo "--- server tail ---"
  tail -n 40 "$SERVER_LOG" 2>/dev/null || true
} | tee -a "$RUN_LOG"

echo "done=1" | tee -a "$RUN_LOG"

# Prefer renderer status if it failed; otherwise propagate headless (may be 1 when thresholds fail).
if [ "$RENDER_STATUS" -ne 0 ]; then
  exit "$RENDER_STATUS"
fi
exit "$HEADLESS_STATUS"
