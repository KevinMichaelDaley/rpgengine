---
id: rpg-r9ns
status: open
deps: [rpg-c6o7]
links: []
created: 2026-03-12T06:48:52Z
type: task
priority: 2
assignee: KMD
parent: rpg-0n7d
---
# §5.4 Inline Physics Simulation (Client-Local Runner)

See ref/scene_editor_design.md §5.4. anim_sim_runner_t: client-local physics world copy for one skeleton; initializes from server state snapshot; steps independently. Play/pause (no server round-trip per tick). Step forward (single/10-tick). Reset to bind pose. Drag (spring constraint to cursor). Apply force (Ctrl+click). Pin (right-click). State display in toolbar. Record commit (baked keyframes sent to server).

## Acceptance Criteria

Play/pause/step/reset simulation. Drag bones during sim. Local runner independent of server tick. Cross-reference with ref/scene_editor_spec.md and ref/scene_editor_ux.md for requirements and interaction details.

