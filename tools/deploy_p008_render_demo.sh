#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  tools/deploy_p008_render_demo.sh [--host IP] [--user USER] [--key PATH]
                                 [--remote-dir DIR] [--port PORT]
                                 [--duration-ms MS] [--tick-hz HZ] [--workers N]
                                 [--headless-clients N] [--seed U32]
                                 [--netem-spec SPEC] [--netem-dev DEV]
                                 [--stop-server-on-exit]
                                 [--dry-run]

Deploys the repo to a remote machine via rsync-over-ssh, builds the P_008
replication server on the remote (REMOTE BUILD REQUIRED), starts it there,
then runs:
  - 1 local rendered client (SDL2/OpenGL)
  - N local headless clients
against the remote server.

Defaults:
  --host             155.138.211.186
  --user             root
  --key              ~/.ssh/vultr
  --remote-dir        /root/rpg
  --port             40080
  --duration-ms      15000
  --tick-hz          60
  --workers          4
  --headless-clients 15
  --seed             123

Netem (optional):
  --netem-spec       tc-netem spec applied on the remote egress, e.g.:
                       "delay 100ms loss 2%"
  --netem-dev        network device to apply qdisc to (default: auto-detect
                     via `ip route get 1.1.1.1` on the remote)

Behavior:
- By default, the remote server is left running after the script exits.
- Use --stop-server-on-exit to kill the remote server during cleanup.

Notes on quoting:
- This script uses single-quoted heredocs for remote bash (`<<'EOF'`) to avoid
  accidental local shell expansion (same pattern as existing deploy scripts).
EOF
}

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd -- "$SCRIPT_DIR/.." && pwd)

HOST="155.138.211.186"
USER="root"
KEY="$HOME/.ssh/vultr"
REMOTE_DIR="/root/rpg"
PORT="40080"
DURATION_MS="15000"
TICK_HZ="60"
WORKERS="4"
HEADLESS_CLIENTS="15"
SEED="123"
DRY_RUN=0
STOP_SERVER_ON_EXIT=0
NETEM_SPEC=""
NETEM_DEV=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    -h|--help) usage; exit 0 ;;
    --host) HOST="$2"; shift 2 ;;
    --user) USER="$2"; shift 2 ;;
    --key) KEY="$2"; shift 2 ;;
    --remote-dir) REMOTE_DIR="$2"; shift 2 ;;
    --port) PORT="$2"; shift 2 ;;
    --duration-ms) DURATION_MS="$2"; shift 2 ;;
    --tick-hz) TICK_HZ="$2"; shift 2 ;;
    --workers) WORKERS="$2"; shift 2 ;;
    --headless-clients) HEADLESS_CLIENTS="$2"; shift 2 ;;
    --seed) SEED="$2"; shift 2 ;;
    --netem-spec) NETEM_SPEC="$2"; shift 2 ;;
    --netem-dev) NETEM_DEV="$2"; shift 2 ;;
    --stop-server-on-exit) STOP_SERVER_ON_EXIT=1; shift ;;
    --dry-run) DRY_RUN=1; shift ;;
    *) echo "Unknown arg: $1" >&2; usage >&2; exit 2 ;;
  esac
done

if [[ ! -f "$KEY" ]]; then
  echo "SSH key not found: $KEY" >&2
  exit 2
fi

SSH=(ssh -i "$KEY" -o BatchMode=yes -o StrictHostKeyChecking=accept-new -o ConnectTimeout=8 -o ServerAliveInterval=5 -o ServerAliveCountMax=2 -o ConnectionAttempts=3)
RSYNC_SSH=(ssh -i "$KEY" -o BatchMode=yes -o StrictHostKeyChecking=accept-new -o ConnectTimeout=8 -o ServerAliveInterval=5 -o ServerAliveCountMax=2 -o ConnectionAttempts=3)

run_cmd() {
  if [[ "$DRY_RUN" -eq 1 ]]; then
    echo "+ $*"
  else
    "$@"
  fi
}

remote() {
  # Avoid remote dotfiles affecting the environment: use `bash -s` with a small prelude.
  local cmd="$1"
  local script
  script=$'set -euo pipefail\n'
  script+="$cmd"
  script+=$'\n'

  if [[ "$DRY_RUN" -eq 1 ]]; then
    echo "+ remote: $cmd"
    return 0
  fi

  "${SSH[@]}" "$USER@$HOST" -- bash -s <<< "$script"
}

ssh_quote_single_() {
  # Quotes a string for safe use inside a single-quoted bash string.
  # shellcheck disable=SC1003
  printf "%s" "$1" | sed "s/'/'\\\\''/g"
}

remote_cmd_tty_() {
  # Run a command via bash -lc without consuming stdin (sudo can prompt).
  local cmd="$1"
  local quoted
  quoted="$(ssh_quote_single_ "$cmd")"

  if [[ "$DRY_RUN" -eq 1 ]]; then
    echo "+ remote-tty: $cmd"
    return 0
  fi

  "${SSH[@]}" -tt "$USER@$HOST" -- bash -lc "'$quoted'"
}

validate_netem_inputs_() {
  if [[ -z "$NETEM_SPEC" ]]; then
    return 0
  fi
  if [[ "$NETEM_SPEC" == *$'\n'* || "$NETEM_SPEC" == *$'\r'* ]]; then
    echo "ERROR: --netem-spec must not contain newlines" >&2
    exit 2
  fi
  if [[ ! "$NETEM_SPEC" =~ ^[a-zA-Z0-9.%_:=,+/\ -]+$ ]]; then
    echo "ERROR: --netem-spec contains unsupported characters" >&2
    echo "       Allowed: letters, digits, space, and .%_:=,+/-" >&2
    exit 2
  fi
  if [[ -n "$NETEM_DEV" && ! "$NETEM_DEV" =~ ^[a-zA-Z0-9._:-]+$ ]]; then
    echo "ERROR: --netem-dev contains unsupported characters" >&2
    exit 2
  fi
}

remote_detect_netem_dev_() {
  if [[ -n "$NETEM_DEV" ]]; then
    echo "$NETEM_DEV"
    return 0
  fi

  remote "ip route get 1.1.1.1 | awk '{for(i=1;i<=NF;i++) if(\$i==\"dev\") {print \$(i+1); exit}}'" | tr -d '\r\n'
}

remote_netem_apply_() {
  if [[ -z "$NETEM_SPEC" ]]; then
    return 0
  fi
  validate_netem_inputs_
  local dev
  dev="$(remote_detect_netem_dev_ || true)"
  if [[ -z "$dev" ]]; then
    echo "ERROR: failed to auto-detect remote net device for netem" >&2
    exit 1
  fi
  echo "==> remote netem: dev=$dev spec=\"$NETEM_SPEC\"" >&2
  # If we need sudo, do NOT use the stdin-fed runner (`remote`), because sudo
  # may need to read a password. Use a TTY command runner instead.
  if remote "[ \"\$(id -u)\" -eq 0 ]"; then
    remote "command -v tc >/dev/null 2>&1 || { echo 'ERROR: tc not found on remote (install iproute2)'; exit 2; }; \
      tc qdisc replace dev '$dev' root netem $NETEM_SPEC"
  else
    remote_cmd_tty_ "command -v tc >/dev/null 2>&1 || { echo 'ERROR: tc not found on remote (install iproute2)'; exit 2; }; \
      command -v sudo >/dev/null 2>&1 || { echo 'ERROR: sudo not found on remote (need root for tc netem)'; exit 2; }; \
      sudo tc qdisc replace dev '$dev' root netem $NETEM_SPEC"
  fi
  remote "echo '$dev' > /tmp/p008_netem_dev" || true
}

remote_netem_clear_() {
  if [[ -z "$NETEM_SPEC" ]]; then
    return 0
  fi
  validate_netem_inputs_

  local dev
  dev="$(remote "cat /tmp/p008_netem_dev 2>/dev/null || true" | tr -d '\r\n')"
  if [[ -z "$dev" ]]; then
    return 0
  fi

  echo "==> remote netem clear: dev=$dev" >&2

  if remote "[ \"\$(id -u)\" -eq 0 ]"; then
    remote "command -v tc >/dev/null 2>&1 && tc qdisc del dev '$dev' root >/dev/null 2>&1 || true" || true
  else
    if remote "command -v sudo >/dev/null 2>&1 && sudo -n true >/dev/null 2>&1"; then
      remote "sudo -n tc qdisc del dev '$dev' root >/dev/null 2>&1 || true" || true
    else
      echo "==> remote netem clear needs sudo password" >&2
      remote_cmd_tty_ "sudo tc qdisc del dev '$dev' root" || true
    fi
  fi

  remote "rm -f /tmp/p008_netem_dev" || true
}

cleanup_() {
  if [[ "$DRY_RUN" -eq 1 ]]; then
    return 0
  fi

  # Wait for any local headless clients.
  if (( ${#pids[@]} > 0 )); then
    for pid in "${pids[@]}"; do
      wait "$pid" || true
    done
  fi

  # Stop remote server best-effort (optional).
  if [[ "$STOP_SERVER_ON_EXIT" -eq 1 ]]; then
    remote "if [ -f /tmp/p008_repl_server.pid ]; then kill \"\$(cat /tmp/p008_repl_server.pid)\" >/dev/null 2>&1 || true; fi" || true
  fi

  # Clear remote netem (optional).
  remote_netem_clear_ || true
}

server_duration_ms() {
  # add some slack so late packets and keepalive don’t race shutdown
  local d="$1"
  if [[ "$d" =~ ^[0-9]+$ ]]; then
    echo $((d + 1500))
  else
    echo "$d"
  fi
}

SERVER_DURATION_MS="$(server_duration_ms "$DURATION_MS")"
TOTAL_CLIENTS=$((HEADLESS_CLIENTS + 1))

if [[ "$STOP_SERVER_ON_EXIT" -eq 0 ]]; then
  # Leave the server running until you stop it.
  SERVER_DURATION_MS="0"
fi

pids=()
trap cleanup_ EXIT INT TERM

echo "==> deploy to $USER@$HOST:$REMOTE_DIR" >&2
remote "mkdir -p '$REMOTE_DIR'"

run_cmd rsync -az --delete \
  -e "${RSYNC_SSH[*]}" \
  --exclude '.git/' \
  --exclude 'build/' \
  --exclude '.beads/' \
  "$REPO_ROOT/" "$USER@$HOST:$REMOTE_DIR/"

echo "==> remote build (headless)" >&2
remote "cd '$REMOTE_DIR' && rm -rf build && make -B build/p008_net_repl_server"

remote_netem_apply_

echo "==> start remote server" >&2
remote "cd '$REMOTE_DIR' && ulimit -n 65536 || true; \
  if [ -f /tmp/p008_repl_server.pid ]; then kill \"\$(cat /tmp/p008_repl_server.pid)\" >/dev/null 2>&1 || true; fi; \
  if command -v ss >/dev/null 2>&1; then \
    for p in \$(ss -H -lunp 2>/dev/null | grep -E ':${PORT}\\b' | sed -n 's/.*pid=\\([0-9][0-9]*\\).*/\\1/p' | sort -u); do kill "\$p" >/dev/null 2>&1 || true; done; \
  fi; \
  rm -f /tmp/p008_repl_server.out; \
  nohup ./build/p008_net_repl_server '$PORT' '$TOTAL_CLIENTS' '$SERVER_DURATION_MS' '$TICK_HZ' '$WORKERS' > /tmp/p008_repl_server.out 2>&1 < /dev/null & echo \$! > /tmp/p008_repl_server.pid"

if [[ "$DRY_RUN" -eq 1 ]]; then
  echo "+ remote: cat /tmp/p008_repl_server.pid" >&2
else
  srv_pid="$(remote "cat /tmp/p008_repl_server.pid" || true)"
  if [[ "$STOP_SERVER_ON_EXIT" -eq 1 ]]; then
    echo "==> remote server pid: ${srv_pid:-unknown} (will be stopped during cleanup)" >&2
  else
    echo "==> remote server pid: ${srv_pid:-unknown} (left running)" >&2
  fi
fi

# wait for ready marker
if [[ "$DRY_RUN" -eq 0 ]]; then
  echo "==> wait for remote readiness" >&2
  if ! remote "for _ in \$(seq 1 200); do \
      grep -q P008_REPL_SERVER_READY /tmp/p008_repl_server.out && exit 0; \
      grep -q 'Failed to bind port' /tmp/p008_repl_server.out && exit 2; \
      sleep 0.05; \
    done; \
    exit 1"; then
    echo "ERROR: remote server did not become ready (see /tmp/p008_repl_server.out on remote)" >&2
    exit 1
  fi
fi

echo "==> build local renderer client" >&2
run_cmd make -s -C "$REPO_ROOT" build/p008_renderer_client

echo "==> run $HEADLESS_CLIENTS headless clients locally" >&2
for i in $(seq 1 "$HEADLESS_CLIENTS"); do
  seed_i=$((SEED + i))
  if [[ "$DRY_RUN" -eq 1 ]]; then
    echo "+ ./build/p008_renderer_client $HOST $PORT $DURATION_MS --seed $seed_i --headless"
  else
    "$REPO_ROOT/build/p008_renderer_client" "$HOST" "$PORT" "$DURATION_MS" --seed "$seed_i" --headless >/tmp/p008_headless_${i}.out 2>&1 &
    pids+=("$!")
  fi
done

echo "==> run rendered client locally (verify multiple cubes)" >&2
if [[ "$DRY_RUN" -eq 1 ]]; then
  echo "+ ./build/p008_renderer_client $HOST $PORT $DURATION_MS --seed $SEED"
else
  "$REPO_ROOT/build/p008_renderer_client" "$HOST" "$PORT" "$DURATION_MS" --seed "$SEED" || true
fi

echo "==> cleanup" >&2
if [[ "$DRY_RUN" -eq 0 ]]; then
  cleanup_ || true
fi

if [[ "$STOP_SERVER_ON_EXIT" -eq 1 ]]; then
  echo "==> remote server was stopped (per --stop-server-on-exit)" >&2
else
  echo "==> remote server left running (stop with: ssh -i '$KEY' $USER@$HOST -- bash -lc 'kill \"\$(cat /tmp/p008_repl_server.pid)\"')" >&2
fi

echo "Done." >&2
