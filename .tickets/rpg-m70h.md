---
id: rpg-m70h
status: open
deps: []
links: []
created: 2026-03-12T06:48:51Z
type: task
priority: 2
assignee: KMD
parent: rpg-zwcc
---
# §2.2 Edit Locking

See ref/scene_editor_design.md §2.2. lock_manager_t, :lock/:freeze/:unlock commands, server-side lock table, lock_notify broadcast, lock visualization in outliner (amber/ice-blue) and viewport (dashed outline), timeout handling, :locks command, :unlock --force.

## Acceptance Criteria

Lock/freeze prevents edits from other editors. Visualization in outliner and viewport. Timeout works. Cross-reference with ref/scene_editor_spec.md and ref/scene_editor_ux.md for requirements and interaction details.

