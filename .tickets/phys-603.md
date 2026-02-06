---
id: phys-603
status: open
deps: [phys-601]
links: [phys-600]
created: 2026-02-06T11:09:00.000000000-08:00
type: task
priority: 1
---
# Step 6.3: Incremental BVH Update


**Parent Epic:** phys-600 (Phase 6: Static BVH)

Handle static body addition/removal without full rebuild.
Refit affected subtrees, rebuild if quality degrades.

## Acceptance Criteria

- [ ] Add/remove static body without full rebuild
- [ ] Quality threshold triggers partial rebuild

