---
id: rust-rpg-it1.10
status: closed
deps: [rust-rpg-it1.1, rust-rpg-it1.9]
links: []
created: 2026-01-18T22:10:20.967939347-08:00
type: task
priority: 2
parent: rust-rpg-it1
---
# P_007: RTT estimator + retransmit scheduling tests

Write RED regression tests for RTT estimation and resend logic.

Covers:
- Resend when ack_bits indicates missing seq
- RTT samples stable (no negative RTT, bounded variance)


