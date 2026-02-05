#!/usr/bin/env bash
set -euo pipefail

BIN=${1:-./build/p000_tests}
ITERS=${ITERS:-100}
TIMEOUT_SECS=${TIMEOUT_SECS:-2}
DUMP_CORE=${DUMP_CORE:-1}

if [[ ! -x "$BIN" ]]; then
  echo "ERROR: binary not found/executable: $BIN" >&2
  exit 2
fi

have_gdb=0
if command -v gdb >/dev/null 2>&1; then
  have_gdb=1
fi

log_dir="build/repro"
mkdir -p "$log_dir"

stamp=$(date -u +%Y%m%dT%H%M%SZ)
log_prefix="$log_dir/p000_hang_${stamp}"

run_one() {
  local i="$1"
  local out_log="${log_prefix}_run${i}.out"
  local gdb_log="${log_prefix}_run${i}.gdb.txt"
  local core_log="${log_prefix}_run${i}.core"

  echo "[${i}/${ITERS}] $BIN (timeout=${TIMEOUT_SECS}s)"

  # Run in background so we can attach on suspected hang.
  ("$BIN" >"$out_log" 2>&1) &
  local pid=$!

  local deadline=$((SECONDS + TIMEOUT_SECS))
  while kill -0 "$pid" 2>/dev/null; do
    if (( SECONDS >= deadline )); then
      echo "TIMEOUT: run ${i} pid=${pid} (capturing diagnostics)" >&2

      if (( have_gdb )); then
        {
          echo "--- gdb thread backtrace (pid=${pid}) ---"
          gdb -q -p "$pid" -batch \
            -ex "set pagination off" \
            -ex "info threads" \
            -ex "thread apply all bt" \
            -ex "detach" \
            -ex "quit"
        } >"$gdb_log" 2>&1 || true
        echo "Wrote $gdb_log" >&2
      else
        echo "NOTE: gdb not found; skipping backtrace" >&2
      fi

      # If we couldn't attach (common when kernel.yama.ptrace_scope != 0), try
      # to produce an offline core dump instead.
      if (( DUMP_CORE )); then
        ulimit -c unlimited || true
        kill -ABRT "$pid" 2>/dev/null || true
        sleep 0.2

        # Try to locate a core file created in this directory and move it into
        # our log directory with a deterministic name.
        local core_found=0
        for c in core core.* core.${pid} core.${pid}.*; do
          if [[ -f "$c" ]]; then
            mv -f "$c" "$core_log" || true
            echo "Wrote $core_log" >&2
            core_found=1
            break
          fi
        done

        # If the system uses systemd-coredump, the core may not be written to
        # the working directory. Try to extract it via coredumpctl.
        if (( core_found == 0 )) && command -v coredumpctl >/dev/null 2>&1; then
          if coredumpctl dump --output "$core_log" "$pid" >/dev/null 2>&1; then
            echo "Wrote $core_log (via coredumpctl)" >&2
          fi
        fi
      fi

      # Try graceful then force.
      kill -TERM "$pid" 2>/dev/null || true
      sleep 0.1
      kill -KILL "$pid" 2>/dev/null || true

      echo "Wrote $out_log" >&2
      return 124
    fi
    sleep 0.01
  done

  # Reap exit code.
  wait "$pid"
}

for ((i=1; i<=ITERS; i++)); do
  run_one "$i"
  rc=$?
  if (( rc != 0 )); then
    echo "FAILED: run $i rc=$rc" >&2
    exit "$rc"
  fi
done

echo "PASS: $ITERS runs finished without timeout"
