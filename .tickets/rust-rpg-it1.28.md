---
id: rust-rpg-it1.28
status: closed
deps: [rust-rpg-it1.1, rust-rpg-it1.3, rust-rpg-it1.15, rust-rpg-it1.9]
links: []
created: 2026-01-18T22:10:23.401915471-08:00
type: task
priority: 2
parent: rust-rpg-it1
---
# P_007: Security/validation + error counter tests

Write RED tests for invalid/malicious input handling.

Covers:
- Protocol ID mismatch drops packet (no state change)
- Unknown schema/module/attribute IDs dropped
- Invalid animation params rejected
- Error counters incremented


