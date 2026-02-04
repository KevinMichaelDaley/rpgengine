---
id: rust-rpg-it1.1
status: closed
deps: []
links: []
created: 2026-01-18T22:10:19.764721832-08:00
type: task
priority: 2
parent: rust-rpg-it1
---
# P_007: Net test utilities (fake clock + loss/jitter)

Build deterministic networking test helpers: injected clock, scripted packet loss/dup/reorder/jitter, and byte-buffer helpers.

Acceptance:
- Deterministically reproduce loss/jitter patterns
- No real sockets required for unit tests


