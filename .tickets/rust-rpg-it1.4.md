---
id: rust-rpg-it1.4
status: closed
deps: [rust-rpg-it1.1, rust-rpg-it1.3]
links: []
created: 2026-01-18T22:10:20.167264022-08:00
type: task
priority: 2
parent: rust-rpg-it1
---
# P_007: ACK window/ack_bits tests (wrap/dup/window)

Write RED tests for ack tracking correctness.

Covers:
- ack_bits mapping (bit0 == ack-1)
- Sequence wraparound near UINT16_MAX
- Duplicate + out-of-window packets ignored


