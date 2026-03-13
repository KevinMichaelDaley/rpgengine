---
id: rpg-dikp
status: closed
deps: []
links: []
created: 2026-03-12T06:48:53Z
type: task
priority: 2
assignee: KMD
parent: rpg-iovj
---
# §7 Engine: Constraint Swap Events

See ref/scene_editor_design.md Engine-Side Work table. Constraint swap events in src/physics/animated/constraint_swap.c.

## Acceptance Criteria

Engine evaluates constraint swap events between ticks. Cross-reference with ref/scene_editor_spec.md and ref/scene_editor_ux.md for requirements and interaction details.


## Notes

**2026-03-13T04:08:51Z**

Closed as duplicate — scope (constraint_swap.c engine eval) is fully covered by §7.1 rpg-9ivc which includes server-side evaluation + editor UI.
