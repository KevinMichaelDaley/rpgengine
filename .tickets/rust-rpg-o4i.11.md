---
id: rust-rpg-o4i.11
status: closed
deps: []
links: []
created: 2026-01-18T18:13:37.588385342-08:00
type: task
priority: 2
parent: rust-rpg-o4i
---
# P_004.12 Skinning: eliminate per-frame mallocs in pipeline_update/draw_list

### Why
P_004 requires skinning to avoid per-frame mallocs. Current implementation allocates in:
- `skinning_pipeline_update` (job contexts)
- `skinning_pipeline_build_draw_list` (temporary palette index array)

### What
- Move temporary allocations to persistent buffers in `skinning_pipeline_t`.
- Ensure update/draw list paths perform zero allocations in steady state.

### Tests
- Add a test that calls update/build_draw_list repeatedly and asserts stability (and optionally uses a debug allocator hook if available).

### Acceptance
- No malloc/free in per-frame code paths.
- Deterministic behavior unchanged.



