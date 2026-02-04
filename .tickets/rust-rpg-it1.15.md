---
id: rust-rpg-it1.15
status: closed
deps: [rust-rpg-it1.14]
links: []
created: 2026-01-18T22:10:21.6394014-08:00
type: task
priority: 2
parent: rust-rpg-it1
---
# P_007: Component schema registry + bit-pack implementation

Implement schema ID registry and bit-pack encode/decode helpers.

Covers:
- No dynamic allocation in encode/decode
- Network byte order
- Explicit decode errors


