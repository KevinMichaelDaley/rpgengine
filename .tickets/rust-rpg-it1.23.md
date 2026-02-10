---
id: rust-rpg-it1.23
status: closed
deps: [rust-rpg-it1.22]
links: []
created: 2026-01-18T22:10:22.709378988-08:00
type: task
priority: 2
parent: rust-rpg-it1
---
# P_007: Reliable snapshot chunking/reassembly

Implement snapshot chunking and reassembly for baseline recovery and large payloads.

Covers:
- Chunk headers + ordering
- No unbounded buffering


