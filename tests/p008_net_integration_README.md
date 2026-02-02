# P_008 UDP replication (headless) — how to run

This directory contains a headless, multi-process UDP replication integration test for P_008.

## Build

```bash
make p008_build
```

## Single-host (loopback) run

### 1) Start the server

```bash
./build/p008_net_repl_server <port> <max_clients> <duration_ms> <tick_hz> <workers>
```

- `duration_ms=0` runs until SIGINT/SIGTERM.

Example (run indefinitely):

```bash
./build/p008_net_repl_server 40001 8 0 60 2
```

Expected output includes:

- `P008_REPL_SERVER_READY`
- A final `p008 stats: ...` line on exit

### 2) Start one or more clients

```bash
./build/p008_net_repl_client <server_ipv4> <server_port> <duration_ms> <expected_spawns>
```

Important:

- `server_port` must match the server’s `<port>`.
- Clients do **not** bind a fixed local port; they use an ephemeral UDP port.

Example (4 clients, each expects 4 spawns):

```bash
./build/p008_net_repl_client 127.0.0.1 40001 1500 4
./build/p008_net_repl_client 127.0.0.1 40001 1500 4
./build/p008_net_repl_client 127.0.0.1 40001 1500 4
./build/p008_net_repl_client 127.0.0.1 40001 1500 4
```

Client thresholds:

- Fails if it observes fewer than `<expected_spawns>` unique SPAWNs.
- Fails if it observes fewer than `expected_spawns * 5` STATE updates.

On failure, the client prints a reason like:

- `Client failed: expected X spawns, got Y ...`
- `Client failed: too few state updates (N)`

## Multi-process integration test (single host)

This is what CI/dev should run locally:

```bash
make test_p008
```

It spawns:

- 1 server process
- N client processes (currently 4)

## Two-host run (server on host A, clients on host B)

1) On host A, build and start the server:

```bash
make p008_build
./build/p008_net_repl_server 40001 16 0 60 2
```

2) On host B, build and start one or more clients, pointing at host A’s IPv4:

```bash
make p008_build
./build/p008_net_repl_client <HOST_A_IPV4> 40001 2000 1
```

Notes:

- Ensure host A allows inbound UDP on the chosen port (e.g. `40001`).
- If NAT/firewall blocks UDP, clients will time out and fail the thresholds.
