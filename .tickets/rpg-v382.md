---
id: rpg-v382
status: open
deps: []
links: []
created: 2026-03-12T06:48:54Z
type: task
priority: 2
assignee: KMD
parent: rpg-5n27
---
# §10.1 Smear-Frame Generation

See ref/scene_editor_design.md §10.1. Compute swept volume between keyframes using GJK support function. Generate mesh hull from swept volume. Render with stretch+fade material. Toggle :anim smear on|off. Per-bone smear threshold.

## Acceptance Criteria

Smear frames generate correctly for high-velocity bones. Toggle works. Cross-reference with ref/scene_editor_spec.md and ref/scene_editor_ux.md for requirements and interaction details.

