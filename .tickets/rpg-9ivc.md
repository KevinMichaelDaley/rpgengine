---
id: rpg-9ivc
status: open
deps: []
links: []
created: 2026-03-12T06:48:53Z
type: task
priority: 2
assignee: KMD
parent: rpg-iovj
---
# §7.1 Constraint Swap Events

See ref/scene_editor_design.md §7.1. ✕ markers on constraint channel in timeline. Right-click to insert at playhead. Inspector: action (replace/remove/add), old/new joint type + params. Trigger conditions (at-frame/on-collision/on-event/on-attribute). Server-side evaluation. Replay on rewind+play. Collision-driven swap (break_strength).

## Acceptance Criteria

Constraint swaps visible in timeline, editable, execute during playback. Collision-driven swap works. Cross-reference with ref/scene_editor_spec.md and ref/scene_editor_ux.md for requirements and interaction details.


## Notes

**2026-03-13T04:08:55Z**

Merged from rpg-dikp: engine file src/physics/animated/constraint_swap.c is part of this ticket's scope.
