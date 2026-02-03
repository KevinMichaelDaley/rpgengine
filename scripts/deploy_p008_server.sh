#!/usr/bin/env bash
set -euo pipefail

# SSH rsync + remote build for P008 server
# - Syncs the full source tree to a remote host
# - Builds p008 binaries via `make p008_build`
# - Optionally starts the server with provided args
#
# Defaults:
#   REMOTE=root@155.138.211.186
#   REMOTE_DIR=/root/rpg
#   PORT=40001 MAX_CLIENTS=72 DURATION_MS=0 TICK_HZ=60 WORKERS=4
#
# Usage examples:
#   ./scripts/deploy_p008_server.sh                      # sync + build only
#   START_SERVER=1 ./scripts/deploy_p008_server.sh       # sync + build + start server
#   START_SERVER=1 PORT=40001 MAX_CLIENTS=64 WORKERS=8 ./scripts/deploy_p008_server.sh
#
# Notes:
# - Requires rsync and ssh locally; make + gcc on remote.
# - Excludes local build artifacts; remote runs a clean build.

REMOTE=${REMOTE:-root@155.138.211.186}
REMOTE_DIR=${REMOTE_DIR:-/root/rpg}

PORT=${PORT:-40001}
MAX_CLIENTS=${MAX_CLIENTS:-72}
DURATION_MS=${DURATION_MS:-0}
TICK_HZ=${TICK_HZ:-60}
WORKERS=${WORKERS:-4}

START_SERVER=${START_SERVER:-0}

ROOT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)

# Sanity checks
command -v rsync >/dev/null 2>&1 || { echo "ERROR: rsync not found" >&2; exit 2; }
command -v ssh   >/dev/null 2>&1 || { echo "ERROR: ssh not found" >&2; exit 2; }

# Create remote directory
echo "Creating remote directory ${REMOTE}:${REMOTE_DIR}..." >&2
ssh "${REMOTE}" "mkdir -p '${REMOTE_DIR}'"

# Rsync source (exclude build artifacts and VCS noise as needed)
echo "Rsyncing source to remote..." >&2
rsync -az --delete \
  --exclude 'build/' \
  --exclude '.git/' \
  --exclude '.vscode/' \
  --exclude '*.o' \
  --exclude '*.a' \
  "${ROOT_DIR}/" "${REMOTE}:${REMOTE_DIR}/"

# Remote build steps
echo "Building on remote via make -B p008_build..." >&2
ssh "${REMOTE}" env REMOTE_DIR="${REMOTE_DIR}" bash -s <<'P008_REMOTE_BUILD'
set -euo pipefail

cd "${REMOTE_DIR}"

if ! command -v make >/dev/null 2>&1; then
  echo 'ERROR: make missing on remote' >&2
  exit 2
fi

# Ensure we don't accidentally reuse build artifacts from another machine.
rm -rf build

make -B p008_build
P008_REMOTE_BUILD

# Optionally start server
if [[ "${START_SERVER}" == "1" ]]; then
  echo "Starting server on remote: port=${PORT} clients=${MAX_CLIENTS} duration_ms=${DURATION_MS} tick_hz=${TICK_HZ} workers=${WORKERS}" >&2
  ssh "${REMOTE}" env REMOTE_DIR="${REMOTE_DIR}" PORT="${PORT}" MAX_CLIENTS="${MAX_CLIENTS}" DURATION_MS="${DURATION_MS}" TICK_HZ="${TICK_HZ}" WORKERS="${WORKERS}" bash -s <<'P008_REMOTE_START'
set -euo pipefail

cd "${REMOTE_DIR}"
ulimit -n 65536 || true

nohup ./build/p008_net_perf_server_tests "${PORT}" "${MAX_CLIENTS}" "${DURATION_MS}" "${TICK_HZ}" "${WORKERS}" > server.out 2>&1 &
echo "$!" > server.pid

i=0
while [[ $i -lt 50 ]]; do
  if grep -q 'P008_REPL_SERVER_READY' server.out; then
    echo 'Server ready'
    exit 0
  fi
  sleep 0.1
  i=$((i + 1))
done

echo 'WARN: server readiness line not yet found'
P008_REMOTE_START
  echo "Server started. To tail logs:" >&2
  echo "ssh ${REMOTE} 'cd ${REMOTE_DIR} && tail -f server.out'" >&2
fi

echo "Done. Remote path: ${REMOTE}:${REMOTE_DIR}"