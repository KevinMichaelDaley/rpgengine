---
id: rust-rpg-bqf
status: closed
deps: []
links: []
created: 2026-02-01T15:01:17.102526347-08:00
type: task
priority: 2
---
# P_008: Replication protocol for cube demo

Define wire protocol for the multi-client cube replication integration/perf test.

Scope
- Reliable messages: JOIN (client->server), SPAWN (server->client) for each client cube; includes initial position and join time.
- Unreliable messages: STATE (server->client) for all remote cubes using quantized vec3 mm + quat snorm16.
- Use existing net primitives: schema registry + bit-pack + packet header.

Deliverables
- RED tests locking in encode/decode layout and determinism.
- Public API in include/ferrum/net/replication/*.h (<=2 public types per header).
- Implementation in src/net/replication/* (<=4 non-static funcs per .c).
- Document representable ranges + error semantics.

Acceptance
- Protocol tests pass; deterministic packing; invalid buffers rejected; range errors surfaced.



