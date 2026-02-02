#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  tools/deploy_net_integration.sh [--host IP] [--user USER] [--key PATH]
                                [--remote-dir DIR] [--port PORT]
                                [--clients N] [--duration-ms MS]
                                [--tick-hz HZ] [--workers N]
                                [--test NAME | --all]
                                [--dry-run]

Deploys the repo to a remote machine via rsync-over-ssh, builds selected headless
network integration server binaries on the remote, runs the server there, then
runs local clients against it while it is running.

Defaults:
  --host        64.176.222.213
  --user        root
  --key         ~/.ssh/vultr
  --remote-dir  ~/rpg
  --port        40001
  --clients     4
  --duration-ms 1500
  --tick-hz     60
  --workers     2
  --test        p008

Tests:
  p008   Remote: ./build/p008_net_repl_server
         Local:  ./build/p008_net_repl_client

Examples:
  tools/deploy_net_integration.sh

  tools/deploy_net_integration.sh --key ~/.ssh/custom_key --user kmd --host 1.2.3.4

  tools/deploy_net_integration.sh --test p008 --clients 8 --port 40010

  tools/deploy_net_integration.sh --all
EOF
}

HOST="64.176.222.213"
USER="root"
KEY="$HOME/.ssh/vultr"
REMOTE_DIR="~/rpg"
PORT="40001"
CLIENTS="4"
DURATION_MS="1500"
TICK_HZ="60"
WORKERS="2"
TEST_NAME="p008"
RUN_ALL=0
DRY_RUN=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    -h|--help)
      usage
      exit 0
      ;;
    --host)
      HOST="$2"; shift 2
      ;;
    --user)
      USER="$2"; shift 2
      ;;
    --key)
      KEY="$2"; shift 2
      ;;
    --remote-dir)
      REMOTE_DIR="$2"; shift 2
      ;;
    --port)
      PORT="$2"; shift 2
      ;;
    --clients)
      CLIENTS="$2"; shift 2
      ;;
    --duration-ms)
      DURATION_MS="$2"; shift 2
      ;;
    --tick-hz)
      TICK_HZ="$2"; shift 2
      ;;
    --workers)
      WORKERS="$2"; shift 2
      ;;
    --test)
      TEST_NAME="$2"; RUN_ALL=0; shift 2
      ;;
    --all)
      RUN_ALL=1; shift
      ;;
    --dry-run)
      DRY_RUN=1; shift
      ;;
    *)
      echo "Unknown arg: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

if [[ ! -f "$KEY" ]]; then
  echo "SSH key not found: $KEY" >&2
  exit 2
fi

SSH=(ssh -i "$KEY" -o BatchMode=yes -o StrictHostKeyChecking=accept-new)
RSYNC_SSH=(ssh -i "$KEY" -o BatchMode=yes -o StrictHostKeyChecking=accept-new)

remote() {
  # shellcheck disable=SC2029
  "${SSH[@]}" "$USER@$HOST" -- bash -lc "$1"
}

run_cmd() {
  if [[ "$DRY_RUN" -eq 1 ]]; then
    echo "+ $*"
  else
    "$@"
  fi
}

sync_repo() {
  echo "==> rsync repo to $USER@$HOST:$REMOTE_DIR"
  run_cmd rsync -az --delete \
    -e "${RSYNC_SSH[*]}" \
    --exclude '.git/' \
    --exclude 'build/' \
    --exclude '.beads/' \
    --exclude '*.o' \
    --exclude '*.a' \
    --exclude '*.so' \
    ./ "$USER@$HOST:$REMOTE_DIR/"
}

build_remote_headless() {
  echo "==> build headless binaries on remote"
  run_cmd remote "cd $REMOTE_DIR && make p008_build"
}

build_local_headless() {
  echo "==> build local client binaries"
  run_cmd make p008_build
}

start_remote_server_p008() {
  local log_file="$REMOTE_DIR/build/p008_server_${PORT}.log"
  local pid_file="$REMOTE_DIR/build/p008_server_${PORT}.pid"

  echo "==> start remote p008 server (port=$PORT)"
  run_cmd remote "mkdir -p $REMOTE_DIR/build"

  # Make stdout line-buffered so READY can be detected quickly.
  run_cmd remote "cd $REMOTE_DIR && nohup stdbuf -oL -eL ./build/p008_net_repl_server $PORT $CLIENTS 0 $TICK_HZ $WORKERS > '$log_file' 2>&1 & echo \$! > '$pid_file'"

  echo "==> wait for server ready"
  if [[ "$DRY_RUN" -eq 1 ]]; then
    return 0
  fi

  local deadline=$((SECONDS + 20))
  while (( SECONDS < deadline )); do
    if remote "grep -q 'P008_REPL_SERVER_READY' '$log_file'"; then
      return 0
    fi
    sleep 0.2
  done

  echo "Server did not become ready; tailing remote log:" >&2
  remote "tail -n 80 '$log_file' || true" >&2
  return 1
}

stop_remote_server_p008() {
  local log_file="$REMOTE_DIR/build/p008_server_${PORT}.log"
  local pid_file="$REMOTE_DIR/build/p008_server_${PORT}.pid"

  echo "==> stop remote p008 server"
  if [[ "$DRY_RUN" -eq 1 ]]; then
    return 0
  fi

  remote "if [[ -f '$pid_file' ]]; then kill \$(cat '$pid_file') 2>/dev/null || true; fi"
  remote "sleep 0.2; if [[ -f '$pid_file' ]]; then kill -9 \$(cat '$pid_file') 2>/dev/null || true; fi"
  remote "tail -n 80 '$log_file' || true"
}

run_local_clients_p008() {
  echo "==> run $CLIENTS local p008 clients against $HOST:$PORT"

  if [[ "$DRY_RUN" -eq 1 ]]; then
    echo "+ ./build/p008_net_repl_client $HOST $PORT $DURATION_MS $CLIENTS (x$CLIENTS)"
    return 0
  fi

  local pids=()
  local i
  for ((i=0; i<CLIENTS; i++)); do
    ./build/p008_net_repl_client "$HOST" "$PORT" "$DURATION_MS" "$CLIENTS" &
    pids+=("$!")
  done

  local ok=0
  for pid in "${pids[@]}"; do
    if wait "$pid"; then
      :
    else
      ok=1
    fi
  done

  return "$ok"
}

run_test() {
  local name="$1"

  case "$name" in
    p008)
      sync_repo
      build_remote_headless
      build_local_headless
      start_remote_server_p008
      if ! run_local_clients_p008; then
        echo "One or more clients failed" >&2
        stop_remote_server_p008
        return 1
      fi
      stop_remote_server_p008
      ;;
    *)
      echo "Unknown test: $name" >&2
      return 2
      ;;
  esac
}

main() {
  if [[ "$RUN_ALL" -eq 1 ]]; then
    run_test p008
    return
  fi

  run_test "$TEST_NAME"
}

main
