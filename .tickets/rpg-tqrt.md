---
id: rpg-tqrt
status: open
deps: [rpg-q6fa, rpg-g169]
links: [rpg-q6fa, rpg-eabp, rpg-m3nv, rpg-2nxx, rpg-ebpv, rpg-hrrd]
created: 2026-03-03T03:01:24Z
type: task
priority: 2
assignee: KMD
---
# Editor integration: animation and skeleton binding (Phase 5)

Wire the animation system into the editor with skeleton entity hierarchy and IL-driven animation control. See ref/renderer_spec.md Phase 5.

Deliverables:
- New editor command 'skeleton_assign' to bind a skeleton asset to a skeletal_mesh entity: loads skeleton hierarchy, creates bone entities as children in scene graph (each bone is an entity with bone_component_t)
- New editor command 'anim_play' to trigger animation clip: anim_play <entity_id> <clip_name> [--loop] [--speed 1.0] [--layer 0] [--blend_time 0.2]
- New editor command 'anim_stop' to stop animation: anim_stop <entity_id> [--layer 0]
- New editor command 'ragdoll_enable' to activate ragdoll: ragdoll_enable <entity_id> [--bone <bone_name>] (per-bone or full-body)
- New editor command 'ragdoll_disable' to return to animation: ragdoll_disable <entity_id> [--blend_time 0.5]
- Bridge callbacks: on_skeleton_assign (creates physics bodies for ragdoll bones with convex colliders), on_anim_play (starts clip evaluation), on_ragdoll_toggle (switches bone from animation to physics or back)
- Add SCRIPT_KEY_ANIM_CLIP (str, key=21) for current animation clip name
- Add SCRIPT_KEY_ANIM_TIME (f32, key=22) for current animation time (normalized 0-1)
- Add SCRIPT_KEY_ANIM_SPEED (f32, key=23) for playback speed multiplier
- Add SCRIPT_KEY_RAGDOLL (bool, key=24) for ragdoll state
- IL animation control: set_field with key=21 to change clip, key=22 to seek, key=23 to adjust speed, key=24 to toggle ragdoll
- IL example: character takes damage -> set_field ragdoll=true on hit bone -> after timer -> set_field ragdoll=false with blend
- Skeleton bones replicated to client as entity hierarchy in spawn messages
- Bone palette upload path: scene_graph world transforms for animated bones -> inv_bind_pose multiply -> bone_palette_buffer_t upload
- Tests for skeleton spawn, anim play/stop, ragdoll toggle, IL animation control

Depends on: rpg-q6fa (material system for skinned shader selection), rpg-g169 (ragdoll implementation)

