---
id: rust-rpg-9r5
status: closed
deps: []
links: []
created: 2026-02-01T22:57:06.964157155-08:00
type: task
priority: 2
---
# Implement ring-buffer-backed topic channels

Implement the ring-buffer-backed topic/channel transport used between the network runtime and gameplay jobs.

Requirements:
- Long ring buffer with message boundaries (length-prefixed or fixed header).
- Supports pushing decoded messages from IO side and popping by consumer jobs.
- Provide backpressure policy (drop oldest/newest, or block-free fail) with metrics.
- No VLAs; prefer fixed headers + dynamic capacity.

Server considerations:
- Per-client fiber-local channels for reliable/unreliable streams.

Client considerations:
- Threaded producer (RX thread) feeding jobs.

Deliverables:
- `src/net/channel/...` style modular implementation.
- Unit tests for wrap-around, full/empty, and boundary integrity.


