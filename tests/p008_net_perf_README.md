# P_008 headless perf + accuracy harness

This harness drives the P_008 headless replication server/client and produces deterministic `key=value` summary lines.

## Build

```bash
make p008_build
```

## Local loopback smoke run

```bash
make p008_perf
```

Expected output includes:
- `P008_PERF_SUMMARY ... thresholds_ok=...`
- `P008_SERVER_STATS ...`

## Two-machine run

On server host (Host A):

```bash
./build/p008_net_perf_server_tests <port> <max_clients> <duration_ms> <tick_hz> <workers> \
  [--drop-pct N] [--jitter-ms N]
```

On client host (Host B):

```bash
./build/p008_net_perf_client_tests <server_ipv4> <port> <clients> <duration_ms> [tick_hz] \
  [--drop-pct N] [--jitter-ms N] \
  [--max-pos-err M] [--max-rot-err-deg M] [--max-state-lag-ms M]
```

Notes:
- `--drop-pct`/`--jitter-ms` are applied via env vars `P008_NET_DROP_PCT` / `P008_NET_JITTER_MS`.
- If thresholds are violated (or a client fails), the client harness exits non-zero.
