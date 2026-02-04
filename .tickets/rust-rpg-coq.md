---
id: rust-rpg-coq
status: open
deps: []
links: []
created: 2026-02-01T22:57:07.635987877-08:00
type: feature
priority: 2
---
# Refactor RUDP layering: retransmit+reassembly above protocol frames

Refactor the networking stack layering so retransmission and message reconstruction occur above protocol framing.

Problem:
- Current RUDP peer mixes protocol frame concerns with how subsystems consume messages.

Requirements:
- Keep a minimal wire-level framing layer (protocol id, schema/topic id, ack header).
- Implement a distinct reliability/reassembly layer that outputs an abstract per-channel stream of decoded messages.
- Ensure subsystems consume only stream/channel messages, not protocol frames.

Deliverables:
- New module boundaries and includes reflected in aggregator headers.
- Migration plan: adapt existing p007/p008 tests and clients/servers.
- Tests for ack/retransmit behavior at the reliability layer boundary.


