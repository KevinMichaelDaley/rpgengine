---
id: rust-rpg-it1.22
status: open
deps: [rust-rpg-it1.1, rust-rpg-it1.21, rust-rpg-it1.9]
links: []
created: 2026-01-18T22:10:22.574534976-08:00
type: task
priority: 2
parent: rust-rpg-it1
---
# P_007: Reliable snapshot chunking/reassembly tests

Write RED tests for chunked full/partial snapshot recovery.

Covers:
- Chunk send over reliable ordered
- Reassembly determinism
- Includes manifests/custom attributes when needed


