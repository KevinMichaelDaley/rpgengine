---
id: rust-rpg-o4i.6
status: closed
deps: [rust-rpg-o4i.2, rust-rpg-o4i.5]
links: []
created: 2026-01-18T10:58:29.364404761-08:00
type: task
priority: 2
parent: rust-rpg-o4i
---
# P_004.6 Skinning shader + attribute semantics

## P_004.6 Skinning shader + attribute semantics

### Goal
Implement skinning shader program(s) and define attribute semantics for weights/indices.

### Scope
- Skinning vertex shader applying weighted bone transforms.
- Attribute layout locations/types documented and enforced.
- Ensure palette bound and uniforms set before draw.

### Tests
- Skinning shader link success and uniform setup.
- Skinning shader link failure captured and reported.
- Attribute semantics lock-in regression test.



