---
id: rust-rpg-it1.5
status: closed
deps: [rust-rpg-it1.4]
links: []
created: 2026-01-18T22:10:20.301722433-08:00
type: task
priority: 2
parent: rust-rpg-it1
---
# P_007: ACK window/ack_bits implementation

Implement receive-window tracking to produce (ack, ack_bits) and update window on inbound packets.

Covers:
- Wrap-safe comparisons
- Duplicate/out-of-window filtering


