---
id: rust-rpg-it1.26
status: closed
deps: [rust-rpg-it1.1, rust-rpg-it1.19, rust-rpg-it1.13, rust-rpg-it1.21]
links: []
created: 2026-01-18T22:10:23.113832265-08:00
type: task
priority: 2
parent: rust-rpg-it1
---
# P_007: Client prediction + reconciliation tests

Write RED tests for client-side prediction + reconciliation.

Covers:
- Input ring buffer with timestamps
- Rewind to last confirmed state and resimulate
- Thresholds avoid micro-corrections; hard snaps on large error


