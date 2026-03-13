---
id: rpg-nyt9
status: closed
deps: []
links: []
created: 2026-03-12T06:48:53Z
type: task
priority: 2
assignee: KMD
parent: rpg-iovj
---
# §7 Engine: Event-to-Animation Binding

See ref/scene_editor_design.md Engine-Side Work table. Event-to-animation binding in src/animation/state/anim_event_trigger.c.

## Acceptance Criteria

Engine triggers animation playback on event receipt. Cross-reference with ref/scene_editor_spec.md and ref/scene_editor_ux.md for requirements and interaction details.


## Notes

**2026-03-13T04:08:51Z**

Closed as duplicate — scope (anim_event_trigger.c engine binding) is fully covered by §7.3 rpg-mrzo which includes server-side event bus + editor UI.
