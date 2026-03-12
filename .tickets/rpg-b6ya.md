---
id: rpg-b6ya
status: open
deps: []
links: []
created: 2026-03-12T06:48:52Z
type: task
priority: 2
assignee: KMD
parent: rpg-hasf
---
# §4.1 Texture Layer System

See ref/scene_editor_design.md §4.1. texture_layer_t (per-layer RGBA8 pixel buffer, name, blend mode, opacity, visibility, lock). texture_layer_stack_t (dynamic array, active layer index). Blend mode compositing (CPU reference; GPU fast path later). Layer operations (add/remove/duplicate/merge down/reorder/flatten). Per-layer resolution support. Terrain splatmap layers as special case.

## Acceptance Criteria

Layer stack works: add/remove/reorder, blend modes composite correctly. Cross-reference with ref/scene_editor_spec.md and ref/scene_editor_ux.md for requirements and interaction details.

