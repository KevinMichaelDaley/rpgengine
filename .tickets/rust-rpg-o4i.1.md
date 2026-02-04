---
id: rust-rpg-o4i.1
status: closed
deps: []
links: []
created: 2026-01-18T10:58:28.703867591-08:00
type: task
priority: 2
parent: rust-rpg-o4i
---
# P_004.1 GL loader + required entry points

## P_004.1 GL loader + required entry points

### Goal
Define the OpenGL loader interface (function pointer table) and required entry points with explicit validation for missing pointers.

### Scope
- Define a loader struct with function pointers for all GL calls used by P_004.
- Provide validation helpers to check required pointers are non-NULL.
- Document ownership and usage expectations in the public header.

### Tests
- Missing GL function pointers return explicit failure.
- Wrapper init/create fails when required entry points are NULL.



