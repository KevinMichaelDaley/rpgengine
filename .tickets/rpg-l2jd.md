---
id: rpg-l2jd
status: open
deps: [rpg-p2vr]
links: []
created: 2026-03-02T18:39:12Z
type: task
priority: 2
assignee: KMD
---
# Phase 5a: animation clip evaluation and blend tree

Create the animation clip sampling and blend tree system. See ref/renderer_spec.md §6.1-6.3.

Deliverables:
- include/ferrum/renderer/anim/anim_clip.h: anim_channel_t enum (TRANSLATION/ROTATION/SCALE), anim_keyframe_t (time + vec3/quat union), anim_track_t (bone_index, channel, keyframes array), anim_clip_t (name, duration, tracks, looping flag)
- include/ferrum/renderer/anim/anim_blend.h: Blend tree nodes (additive layers, override layers with bone masks, cross-fade with configurable duration), anim_blend_tree_t, anim_blend_evaluate()
- src/renderer/anim/anim_clip_eval.c: Sample clip at time T with lerp (vec3) / slerp (quat), binary search for keyframe bracket, wrap for looping clips
- src/renderer/anim/anim_blend.c: Layered blending with per-bone weight masks, additive and override modes, smooth cross-fade transitions
- Outputs local bone transforms (vec3 translation, quat rotation, vec3 scale) per bone
- Tests in tests/p004_renderer_anim_clip_tests.c

Depends on: rpg-p2vr (skeletal_mesh_t provides bone structure)

