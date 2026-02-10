---
id: rust-rpg-it1.12
status: closed
deps: [rust-rpg-it1.1, rust-rpg-it1.11]
links: []
created: 2026-01-18T22:10:21.23694897-08:00
type: task
priority: 2
parent: rust-rpg-it1
---
# P_007: Time sync + jitter buffer tests

Write RED tests for time synchronization.

Covers:
- Offset estimation = median half-RTT
- Jitter buffer produces smooth interpolation times
- Drift clamp avoids visual pops


