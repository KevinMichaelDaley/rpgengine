---
id: rpg-20r1
status: closed
deps: []
links: []
created: 2026-02-13T07:27:03Z
type: task
priority: 2
assignee: KMD
---
# Built-in network condition emulator (delay, jitter, loss, reorder)

Implement a network condition emulator as a core engine feature (not demo-specific). Should sit between the RUDP send/receive path and the actual UDP socket, injecting configurable latency, jitter, packet loss, and reorder — similar to tc netem but in-process with finer control.

Key requirements:
- Core library module (e.g. src/net/emulation/), not specialized inside the demo
- Demo and tests invoke it via API (e.g. net_emulator_config_t with delay_ms, jitter_ms, loss_pct, reorder_pct)
- Support asymmetric conditions (server→client vs client→server can differ, emulating fast server / slow client)
- Realistic jitter distributions (normal/log-normal, not just uniform ±range)
- Per-packet delay queue with timer-based release
- Enable/disable at runtime without restart
- Zero overhead when disabled (compile-time or runtime bypass)
- Should work for both send and receive paths so either side can emulate independently

This replaces the need for external tc/netem for development and testing, and allows automated tests to exercise network edge cases programmatically.

