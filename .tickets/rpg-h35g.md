---
id: rpg-h35g
status: open
deps: [rpg-8xow]
links: []
created: 2026-03-12T06:48:54Z
type: task
priority: 2
assignee: KMD
parent: rpg-h6z6
tags: [visual-test]
---
# Phase 9 Visual Test (v9_streaming)

See ref/scene_editor_design.md §9.4 visual test. Load 10K+ entity test scene. Swap distant groups to disk, verify wireframe AABBs. Swap back, verify restore. Navigate rapidly, verify LOD transitions and async loading without hitches. Verify interactive frame rate. Includes all prior phase checks.

## Acceptance Criteria

visual/v9_streaming test passes. All Phase 0-1+9 features functional. Cross-reference with ref/scene_editor_spec.md and ref/scene_editor_ux.md for requirements and interaction details.

