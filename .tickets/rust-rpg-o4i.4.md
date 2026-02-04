---
id: rust-rpg-o4i.4
status: closed
deps: [rust-rpg-o4i.1]
links: []
created: 2026-01-18T10:58:29.103136996-08:00
type: task
priority: 2
parent: rust-rpg-o4i
---
# P_004.4 VBO/VAO create/destroy + attribute layout

## P_004.4 VBO/VAO create/destroy + attribute layout

### Goal
Implement buffer/vertex array wrappers with explicit lifetime control and attribute binding.

### Scope
- VBO create/destroy; upload data (size + pointer).
- VAO create/destroy; bind attribute layouts (format/stride/offset).
- Double-destroy safe (no double delete).
- Zero-size uploads are no-op or explicit error (document).

### Tests
- VBO/VAO create/destroy pairs; delete called once.
- Attribute layout binding passes correct strides/offsets.
- Double destroy is safe.
- Zero-size upload behavior.



