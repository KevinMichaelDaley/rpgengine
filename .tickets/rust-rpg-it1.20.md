---
id: rust-rpg-it1.20
status: open
deps: [rust-rpg-it1.1, rust-rpg-it1.15, rust-rpg-it1.19, rust-rpg-it1.9]
links: []
created: 2026-01-18T22:10:22.307115439-08:00
type: task
priority: 2
parent: rust-rpg-it1
---
# P_007: Snapshot baseline + delta replication tests

Write RED tests for baseline tracking and delta application.

Covers:
- Delta from baseline reaches target state
- Client ACKs snapshot IDs; baseline advances
- Partial component update merges correctly
- Missing baseline fallback trigger


