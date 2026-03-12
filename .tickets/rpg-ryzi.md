---
id: rpg-ryzi
status: open
deps: [rpg-7s7r]
links: []
created: 2026-03-12T06:48:53Z
type: task
priority: 2
assignee: KMD
parent: rpg-o25i
tags: [visual-test]
---
# Phase 6 Visual Test (v6_timeline)

See ref/scene_editor_design.md §6.7 visual test. Open timeline in Animation mode. Insert position/rotation keyframes via I key. Insert physics keyframes. Play back with physics coupling. Tune damping, verify velocity in inspector. Record sim pass (Ctrl+R), verify baked keyframes. Bake rigid body (:anim bake sim). Run :anim bake clean. Set time selection, verify loop. Includes all prior phase checks.

## Acceptance Criteria

visual/v6_timeline test passes. All Phase 0-1+5+6 features functional. Cross-reference with ref/scene_editor_spec.md and ref/scene_editor_ux.md for requirements and interaction details.

