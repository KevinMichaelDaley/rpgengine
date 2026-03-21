---
id: rpg-air9
status: closed
deps: []
links: []
created: 2026-03-17T06:47:34Z
type: feature
priority: 1
assignee: KMD
tags: [editor, renderer, skeleton, mesh]
---
# Skeletal mesh rendering + per-entity static→skeletal switching

## Summary

Wire skeletal mesh rendering into the editor entity pipeline. An entity with a static mesh that gets assigned an .fskel skeleton should automatically promote to a skeletal mesh (if the FVMA has bone weight data). The reverse switch (skeletal→static) must NOT be enabled — it is lossy/destructive and would cause accidental data loss.

## Background

The renderer already supports `skeletal_mesh_t` (VBOs for bone weights at location 6, bone indices at location 7, inverse bind matrices). The FVMA binary format already has `MESH_VAO_FLAG_BONES` for skeletal data. `skeletal_mesh_create_from_fvma()` in `src/renderer/mesh/skeletal_mesh_fvma.c` handles the full load path including weight normalization. The bone palette system (SSBO/UBO/TBO) is implemented.

What's missing is the editor integration: per-entity mesh type tracking, the skeleton assignment flow, and the rendering dispatch.

## Deliverables

### Per-Entity Mesh Type Tracking
- Add `viewport_mesh_type_t` enum: `VIEWPORT_MESH_NONE`, `VIEWPORT_MESH_STATIC`, `VIEWPORT_MESH_SKELETAL`
- Add per-entity type array to `viewport_render_state_t` (parallel to `entity_mesh_cache`)
- Collision overlay (`scene_viewport_collision_overlay.c`) dispatches on mesh type
- When `snap_mesh_should_decompose()` fires, upload the decomposed snap mesh as a collision overlay GPU mesh so pressing C shows the convex hull wireframe, not the raw mesh

### Skeleton Assignment Flow
- When `SCRIPT_KEY_SKEL_PATH` is set on a MESH entity that already has a loaded static mesh:
  1. Check if the FVMA has `MESH_VAO_FLAG_BONES`
  2. If yes: destroy the static mesh, reload as `skeletal_mesh_t`, update mesh type to SKELETAL
  3. If no: log warning "mesh has no bone weights, cannot bind skeleton" (future: weight paint mode will enable painting weights onto static meshes)
- Store skeleton binding: track which `edit_skeleton_registry_t` entry is bound to each entity
- The entity's snap mesh (convex decomposition for high-poly, raw for low-poly) remains unchanged — snap is always static geometry

### Rendering Dispatch
- `scene_viewport_draw.c`: check mesh type per entity; for SKELETAL, use the bone palette + skinning shader instead of the static Blinn-Phong shader
- Upload bone matrices each frame for animated entities (initially: bind pose only — full animation playback is a separate ticket)
- The collision overlay for skeletal meshes falls back to the static collision mesh cache (convex hulls) or the snap mesh if no explicit collision mesh is loaded

### Safety
- Switching static→skeletal: allowed when fskel is assigned and FVMA has bone data
- Switching skeletal→static: DISABLED. No command, no UI, no code path. Print error if attempted: "Cannot strip skeleton binding — operation is destructive and irreversible"

## Key Files
- `include/ferrum/editor/scene/scene_viewport_render.h` — add mesh type enum + per-entity type array
- `src/editor/scene/scene_viewport_mesh.c` — detect FVMA bone flag, choose static vs skeletal create
- `src/editor/scene/scene_viewport_draw.c` — dispatch rendering by mesh type
- `src/editor/scene/scene_viewport_collision_overlay.c` — dispatch overlay by mesh type; show decomposed snap mesh for high-poly statics
- `src/editor/scene/scene_frame_sync.c` — trigger skeleton assignment when SKEL_PATH attr is set
- `src/editor/scene/scene_asset_load.c` — add `scene_load_entity_skeleton()` or extend existing

## Acceptance Criteria
- [ ] Loading humanoid.fvma + humanoid.fskel on the same entity renders a skinned mesh in bind pose
- [ ] Pressing C on a high-poly static mesh shows convex hull wireframe, not raw mesh wireframe
- [ ] Attempting to remove skeleton from a skeletal mesh entity prints error and does nothing
- [ ] Static mesh entities without fskel render exactly as before (no regression)
- [ ] Per-entity mesh type survives save/load cycle (persisted in entity attrs or scene file)

