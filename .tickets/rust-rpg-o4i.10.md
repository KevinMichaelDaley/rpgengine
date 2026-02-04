---
id: rust-rpg-o4i.10
status: closed
deps: [rust-rpg-o4i.9]
links: []
created: 2026-01-18T18:13:37.578821712-08:00
type: task
priority: 2
parent: rust-rpg-o4i
---
# P_004.11 Render pipeline: remove static global pass storage

### Why
`render_pipeline_default` currently uses a static `passes[3]`, which is global mutable state and violates P_004 architectural guidance.

### What
- Remove static storage from `render_pipeline_default`.
- Make pass storage explicitly owned by the caller or by the pipeline instance.

### Tests
- Create two pipelines in the same process with different passes and verify they do not alias/overwrite each other.

### Acceptance
- No global mutable state.
- Clear ownership rules documented.



