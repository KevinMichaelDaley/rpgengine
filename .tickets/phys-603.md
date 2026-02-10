---
id: phys-603
status: closed
deps: [phys-601]
links: [phys-600]
created: 2026-02-06T11:09:00.000000000-08:00
type: task
priority: 1
---
# Step 6.3: Incremental BVH Update


**Parent Epic:** phys-600 (Phase 6: Static BVH)

Handle static body addition/removal for level editing.
Rebuild the static BVH lazily on the next tick when static geometry changes.

## Acceptance Criteria

- [ ] Adding/removing a static body marks the BVH dirty and rebuilds on next tick
- [ ] Editing a static body's collider marks the BVH dirty and rebuilds on next tick

## Notes

Incremental refit / quality-based partial rebuild can be revisited later if level-edit workflows need it.

