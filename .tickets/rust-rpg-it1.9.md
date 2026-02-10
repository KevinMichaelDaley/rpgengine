---
id: rust-rpg-it1.9
status: closed
deps: [rust-rpg-it1.8]
links: []
created: 2026-01-18T22:10:20.838234032-08:00
type: task
priority: 2
parent: rust-rpg-it1
---
# P_007: Reliable ordered channel implementation

Implement reliable ordered channel: sequencing, buffering, ordered delivery, and integration with ACK tracking.

Covers:
- Retransmit-friendly book-keeping
- No blocking recv


