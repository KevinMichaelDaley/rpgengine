---
id: rust-rpg-o4i.9
status: closed
deps: [rust-rpg-o4i.12]
links: []
created: 2026-01-18T18:13:37.576563744-08:00
type: task
priority: 2
parent: rust-rpg-o4i
---
# P_004.9 Render pipeline: dependency graph + optional depth pre-pass

### Why
Current `render_pipeline.*` is only a linear pass list; P_004 spec requires a minimal graph with explicit dependencies and an optional depth pre-pass node.

### What
- Introduce a minimal pipeline graph representation that can express dependencies between named passes.
- Support an optional depth pre-pass stage that can be enabled/disabled without changing other passes.
- Keep deterministic execution order derived from the graph (topological order; ties deterministic).

### Tests (real GL, no mocking)
- Construct a graph with skybox, (optional) depth-pre, forward, post.
- Verify execution order matches dependencies when depth pre-pass enabled.
- Verify depth pre-pass is skipped when disabled.

### Acceptance
- No global mutable state.
- Deterministic order.
- API stays minimal and explicit.



