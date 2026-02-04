---
id: rust-rpg-rk2
status: open
deps: []
links: []
created: 2026-02-01T22:57:06.669443268-08:00
type: task
priority: 2
---
# Client network runtime: TX thread + outbound topic pump

Implement the client-side network TX thread and outbound topic pump.

Requirements:
- Poll outbound topic/channel ring buffers for messages to send.
- Serialize messages into per-channel packets and apply reliability framing (if reliable channel) before sending via UDP.
- Rate-limit / batch to avoid bursts that overflow send-slot storage.
- Coordinate with RX thread for shared reliability state (explicit synchronization strategy; avoid data races).

Notes:
- This runs on an OS thread; `malloc` is allowed.

Deliverables:
- Outbound queue/topic API used by gameplay jobs.
- Thread lifecycle + shutdown.
- Basic tests for batching/rate limiting behavior.


