---
id: rpg-92qq
status: open
deps: [rpg-cf2c]
links: []
created: 2026-03-12T06:48:53Z
type: task
priority: 2
assignee: KMD
parent: rpg-0n7d
tags: [visual-test]
---
# Phase 5 Visual Test (v5_rigging)

See ref/scene_editor_design.md §5.7 visual test. Import mesh, enter Animation mode. Place 5-bone chain, extrude. Set cone-twist joints, verify limit cones. Install muscles, verify attachment lines and wrap cylinders. Run physics sim (Space), drag bones, apply impulse, pin bone. Verify local runner independence. Includes all prior phase checks.

## Acceptance Criteria

visual/v5_rigging test passes. All Phase 0-1+5 features functional. Cross-reference with ref/scene_editor_spec.md and ref/scene_editor_ux.md for requirements and interaction details.

