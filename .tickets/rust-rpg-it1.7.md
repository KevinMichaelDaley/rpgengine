---
id: rust-rpg-it1.7
status: closed
deps: [rust-rpg-it1.6]
links: []
created: 2026-01-18T22:10:20.573021543-08:00
type: task
priority: 2
parent: rust-rpg-it1
---
# P_007: Unreliable channel ring implementation

Implement the unreliable channel abstraction for high-rate updates (movement/transform deltas).

Covers:
- Fixed-size rings
- No malloc in hot path


