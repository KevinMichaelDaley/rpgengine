---
id: rpg-s9hi
status: closed
deps: []
links: []
created: 2026-02-09T04:35:45Z
type: epic
priority: 2
assignee: KMD
tags: [net, testing, client]
---
# Minimal test client framework

Build a minimal headless test client framework for integration tests.

Goal: allow tests to stand up a server + 1..N clients, drive join/handshake, send inputs, and assert replicated outputs (spawns/state) without SDL/renderer.


## Design

Design sketch:
- Provide a small API usable from C tests:
  - client_create/connect
  - client_pump_rx / client_send
  - client_pop_topic / decode helpers
- Prefer loopback UDP sockets + deterministic scripted link (loss/dup/reorder) where useful.
- Reuse existing RUDP + replication schemas; avoid new dependencies.
- Include time control (fake clock or explicit now_ms) to test resend behavior.

Focus on minimal viable coverage:
- join/welcome
- spawn batch delivery
- state cube updates


## Acceptance Criteria

Acceptance criteria:
- New integration tests can run in CI/headless:
  - server + client join
  - server sends spawn batch; client receives and decodes
  - basic state update roundtrip
- Can inject loss and still converge (reliable delivery).

