---
id: rust-rpg-o4i.3
status: closed
deps: [rust-rpg-o4i.2]
links: []
created: 2026-01-18T10:58:28.97092521-08:00
type: task
priority: 2
parent: rust-rpg-o4i
---
# P_004.3 Uniform setters + location cache

## P_004.3 Uniform setters + location cache

### Goal
Implement shader uniform setters (mat4/vec3/int/float) with cached locations.

### Scope
- Uniform lookup caching per program.
- Setters call correct GL functions for types.
- Optional type checking policy documented.

### Tests
- Uniform upload calls correct GL functions.
- Location cache used after first lookup.
- Uniform type mismatch guard (if implemented) or documented no-check policy.



