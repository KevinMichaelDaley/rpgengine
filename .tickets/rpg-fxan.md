---
id: rpg-fxan
status: closed
deps: []
links: []
created: 2026-03-12T06:48:54Z
type: task
priority: 2
assignee: KMD
parent: rpg-5n27
---
# §10 Engine: Animation Clip Format (.fanim)

See ref/scene_editor_design.md Engine-Side Work table. Animation clip format in src/animation/format/fanim_load.c.

## Acceptance Criteria

Engine can load and save .fanim files. Cross-reference with ref/scene_editor_spec.md and ref/scene_editor_ux.md for requirements and interaction details.


## Notes

**2026-03-13T04:09:16Z**

Closed as duplicate — scope (fanim_load.c load/save) is fully covered by §10.5 rpg-95uc which includes the complete .fanim format spec + all channel types.
