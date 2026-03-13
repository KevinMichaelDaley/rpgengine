---
id: rpg-hln7
status: open
deps: []
links: []
created: 2026-03-12T06:48:53Z
type: task
priority: 2
assignee: KMD
parent: rpg-iovj
---
# §7.4 Animated Attribute Modifiers

See ref/scene_editor_design.md §7.4. Attribute modifier channels in timeline (per-entity). Keyframe arbitrary entity attributes (health, glow, etc.). Interpolation: step/linear/cubic. Server-side evaluation (anim_attr_modifier_t writes to entity_attrs_t). :anim attr command.

## Acceptance Criteria

Animated attribute modifiers drive entity attributes over time. Cross-reference with ref/scene_editor_spec.md and ref/scene_editor_ux.md for requirements and interaction details.


## Notes

**2026-03-13T04:08:55Z**

Merged from rpg-asew: engine file src/animation/attr/anim_attr_eval.c is part of this ticket's scope.
