---
id: rust-rpg-f2j
status: closed
deps: []
links: []
created: 2026-02-01T22:57:06.816020267-08:00
type: feature
priority: 2
---
# Define reliable UDP stream + topic/channel API

Define the public API for a "reliable UDP stream" and topic/channel abstraction.

Scope:
- A per-connection/per-channel stream interface that yields next decoded messages in-order for reliable channels.
- Topic/channel acts like a socket backed by a long ring buffer (bytes + message boundaries).
- Subsystems consume messages from channels; they never see protocol frames.

Requirements:
- Explicit ownership + lifetime rules.
- Works for both client (threaded IO) and server (fiber per client).
- Supports at least: reliable ordered, unreliable.

Deliverables:
- Headers for stream + channel registry.
- Minimal message envelope convention (schema/topic id + payload view).
- Documented concurrency expectations (single-producer/single-consumer vs MPSC) per channel type.


