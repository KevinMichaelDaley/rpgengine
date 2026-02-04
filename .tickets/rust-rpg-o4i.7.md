---
id: rust-rpg-o4i.7
status: closed
deps: [rust-rpg-o4i.2, rust-rpg-o4i.4]
links: []
created: 2026-01-18T10:58:29.499555994-08:00
type: task
priority: 2
parent: rust-rpg-o4i
---
# P_004.7 Render pipeline graph + default passes

## P_004.7 Render pipeline graph + default passes

### Goal
Define pipeline graph structs, pass interfaces, and default pass ordering.

### Scope
- Pass nodes: skybox, optional depth pre-pass, forward main, post stub.
- Stage interfaces: begin_pass, resource bindings, draw submission, end_pass.
- Deterministic execution ordering; no implicit global state.

### Tests
- Pipeline execution ordering (skybox → forward → post stub).
- No implicit global binds: required binds happen per pass.



