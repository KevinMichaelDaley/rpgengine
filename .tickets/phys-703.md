---
id: phys-703
status: closed
deps: [phys-408]
links: [phys-700, rpg-m9nw]
created: 2026-02-06T11:09:00.000000000-08:00
type: task
priority: 1
---
# Step 7.3: Position-Level Solve (Split Impulse)


**Parent Epic:** phys-700 (Phase 7: Advanced Stability)

Separate velocity and position correction for cleaner stacking.
Apply position correction as a separate pass to avoid adding
energy via Baumgarte stabilization.

## Acceptance Criteria

- [ ] Position correction doesn't add energy
- [ ] Stacking quality improved vs pure Baumgarte

