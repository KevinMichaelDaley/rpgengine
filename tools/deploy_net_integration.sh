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
  --remote-dir  /root/rpg
  --port        40001
  --clients     4
  --duration-ms 1500
  --tick-hz     60
  --workers     2
  --test        p008

Tests:
  p008   Remote: ./build/p008_net_repl_server
         Local:  ./build/p008_net_repl_client

  p000   Remote: ./build/p000_tests (job system tests)
         Local:  (none)

Examples:
  tools/deploy_net_integration.sh

  tools/deploy_net_integration.sh --key ~/.ssh/custom_key --user kmd --host 1.2.3.4

  tools/deploy_net_integration.sh --test p008 --clients 8 --port 40010

  tools/deploy_net_integration.sh --all
EOF
}

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd -- "$SCRIPT_DIR/.." && pwd)

HOST="64.176.222.213"
USER="root"
KEY="$HOME/.ssh/vultr"
# Use an absolute path by default to avoid rsync creating a literal "~/" directory.
REMOTE_DIR="/root/rpg"
REMOTE_DIR_RESOLVED=""
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

# Prevent indefinite hangs if the remote becomes unresponsive.
# Override via env var: SSH_TIMEOUT_SECS=30 tools/deploy_net_integration.sh ...
SSH_TIMEOUT_SECS="${SSH_TIMEOUT_SECS:-15}"
SSH_CONNECT_TIMEOUT_SECS="${SSH_CONNECT_TIMEOUT_SECS:-8}"

SSH+=( -o ConnectTimeout="$SSH_CONNECT_TIMEOUT_SECS" -o ServerAliveInterval=5 -o ServerAliveCountMax=2 )
RSYNC_SSH+=( -o ConnectTimeout="$SSH_CONNECT_TIMEOUT_SECS" -o ServerAliveInterval=5 -o ServerAliveCountMax=2 )

remote() {
  # ssh executes the remote command via /bin/sh by default, so keep quoting POSIX-safe.
  # Also avoid `bash -l` (login shell) so remote dotfiles can't pollute output.
  local cmd="$1"
  local script
  script=$'set -euo pipefail\n'
  script+="$cmd"
  script+=$'\n'

  timeout "$SSH_TIMEOUT_SECS" "${SSH[@]}" "$USER@$HOST" -- bash -s <<< "$script"
}

resolve_remote_dir() {
  if [[ -n "$REMOTE_DIR_RESOLVED" ]]; then
    return 0
  fi

  if [[ "$DRY_RUN" -eq 1 ]]; then
    # In dry-run mode we don't probe the remote; assume literal is OK.
    REMOTE_DIR_RESOLVED="$REMOTE_DIR"
    return 0
  fi

  # Resolve leading '~' using the remote HOME.
  if [[ "$REMOTE_DIR" == ~* ]]; then
    local remote_home
    remote_home=$(remote 'printf %s "$HOME"')
    if [[ -z "$remote_home" ]]; then
      echo "Failed to resolve remote HOME" >&2
      return 1
    fi
    if [[ "$REMOTE_DIR" == "~" ]]; then
      REMOTE_DIR_RESOLVED="$remote_home"
    elif [[ "$REMOTE_DIR" == "~/"* ]]; then
      REMOTE_DIR_RESOLVED="$remote_home/${REMOTE_DIR#~/}"
    else
      # Unsupported ~user form; leave as-is.
      REMOTE_DIR_RESOLVED="$REMOTE_DIR"
    fi
  else
    REMOTE_DIR_RESOLVED="$REMOTE_DIR"
  fi
}

run_cmd() {
  if [[ "$DRY_RUN" -eq 1 ]]; then
    echo "+ $*"
  else
    "$@"
  fi
}

sync_repo() {
  resolve_remote_dir
  echo "==> rsync repo to $USER@$HOST:$REMOTE_DIR_RESOLVED"
  # Ensure the remote directory exists even on first deploy.
  run_cmd remote "mkdir -p '$REMOTE_DIR_RESOLVED'"
  run_cmd rsync -az --delete \
    -e "${RSYNC_SSH[*]}" \
    --exclude '.git/' \
    --exclude 'build/' \
    --exclude '.beads/' \
    "$REPO_ROOT/" "$USER@$HOST:$REMOTE_DIR_RESOLVED/"
}

build_remote_headless() {
  echo "==> build headless binaries on remote"
  resolve_remote_dir
  # Force a native rebuild on the remote (rsync may have copied local build/ artifacts).
  run_cmd remote "cd '$REMOTE_DIR_RESOLVED' && rm -f build/p008_net_repl_server build/p008_net_repl_client build/p008_net_multi_client_server_integration_tests && make -B p008_build"
}

build_remote_p000() {
  echo "==> build p000 job system tests on remote"
  resolve_remote_dir
  run_cmd remote "cd '$REMOTE_DIR_RESOLVED' && rm -f build/p000_tests && make -B build/p000_tests"
}

build_local_headless() {
  echo "==> build local client binaries"
  run_cmd make -C "$REPO_ROOT" -B p008_build
}

maybe_open_remote_firewall_udp() {
  resolve_remote_dir
  echo "==> ensure remote firewall allows UDP port $PORT (ufw)"
  # Best-effort: don't fail if ufw isn't installed/active.
  run_cmd remote "if command -v ufw >/dev/null 2>&1; then ufw allow ${PORT}/udp >/dev/null 2>&1 || true; fi"
}

kill_remote_udp_listeners_on_port() {
  resolve_remote_dir
  echo "==> kill any remote p008 server processes (best-effort)"
  run_cmd remote "pids=''; if command -v pidof >/dev/null 2>&1; then pids=\$(pidof p008_net_repl_server || true); fi; if [[ -z \"\$pids\" ]]; then pids=\$(ps ax -o pid= -o args= | grep -F p008_net_repl_server | grep -v grep | awk '{print \$1}' | tr '\n' ' ' || true); fi; echo \"remote p008 pids: [\$pids]\"; if [[ -n \"\$pids\" ]]; then kill \$pids || true; sleep 0.2; kill -9 \$pids || true; fi"
}

start_remote_server_p008() {
  resolve_remote_dir
  local log_file="$REMOTE_DIR_RESOLVED/build/p008_server_${PORT}.log"
  local pid_file="$REMOTE_DIR_RESOLVED/build/p008_server_${PORT}.pid"

  echo "==> start remote p008 server (port=$PORT)"
  kill_remote_udp_listeners_on_port
  run_cmd remote "mkdir -p '$REMOTE_DIR_RESOLVED/build'"

  # Make stdout line-buffered so READY can be detected quickly.
  # IMPORTANT: Redirect stdin from /dev/null; otherwise the backgrounded server can
  # inherit the SSH session stdin and keep the SSH command from returning.
  run_cmd remote "cd '$REMOTE_DIR_RESOLVED' && (nohup stdbuf -oL -eL ./build/p008_net_repl_server $PORT $CLIENTS 0 $TICK_HZ $WORKERS </dev/null > '$log_file' 2>&1 & echo \$! > '$pid_file')"

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
  resolve_remote_dir
  local log_file="$REMOTE_DIR_RESOLVED/build/p008_server_${PORT}.log"
  local pid_file="$REMOTE_DIR_RESOLVED/build/p008_server_${PORT}.pid"

  echo "==> stop remote p008 server"
  if [[ "$DRY_RUN" -eq 1 ]]; then
    return 0
  fi

  # Try pidfile first, then fall back to `ss` PID detection.
  remote "if [[ -f '$pid_file' ]]; then pid=\$(cat '$pid_file' || true); if [[ -n \"\$pid\" ]]; then echo \"remote pidfile pid=\$pid\"; kill \$pid || true; fi; fi"
  remote "sleep 0.2; if [[ -f '$pid_file' ]]; then pid=\$(cat '$pid_file' || true); if [[ -n \"\$pid\" ]]; then kill -9 \$pid || true; fi; fi"
  kill_remote_udp_listeners_on_port
  remote "tail -n 80 '$log_file' || true"
}

run_local_clients_p008() {
  echo "==> run $CLIENTS local p008 clients against $HOST:$PORT"

  if [[ "$DRY_RUN" -eq 1 ]]; then
    echo "+ $REPO_ROOT/build/p008_net_repl_client $HOST $PORT $DURATION_MS $CLIENTS (x$CLIENTS)"
    return 0
  fi

  local pids=()
  local i
  for ((i=0; i<CLIENTS; i++)); do
    "$REPO_ROOT/build/p008_net_repl_client" "$HOST" "$PORT" "$DURATION_MS" 0 &
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
    p000)
      sync_repo
      build_remote_p000
      echo "==> run remote p000 job system tests"
      run_cmd remote "cd '$REMOTE_DIR_RESOLVED' && ./build/p000_tests"
      ;;
    p008)
      sync_repo
      build_remote_headless
      build_local_headless
      maybe_open_remote_firewall_udp
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
    run_test p000
    run_test p008
    return
  fi

  run_test "$TEST_NAME"
}

main
