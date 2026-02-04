---
id: rust-rpg-ml2
status: closed
deps: [rust-rpg-bqf]
links: []
created: 2026-02-01T15:02:20.731710718-08:00
type: task
priority: 2
---
# P_008: Multi-client replication server

Implement a standalone server executable that supports multiple UDP clients and drives the cube replication test.

Scope
- Bind one UDP socket and track multiple client addresses (do not assume 1 client per server).
- For each client: maintain reliable channel state for JOIN/SPAWN logic.
- Broadcast unreliable STATE updates (quantized) to every client each tick.
- Use job system to parallelize per-client encode/send work (multiple network jobs).

Deliverables
- Server integration binary under tests/ (or a dedicated tools/ directory if we decide), runnable on another machine.
- Headless-friendly: no SDL/GL dependency.
- Integration tests covering: multiple clients join, spawn reliability, continuous state updates.

Acceptance
- Can run  and accept N clients.
- Logs basic stats (pps, bytes/s, connected clients, dropped packets).



