---
id: rpg-kte9
status: closed
deps: [rpg-nuxr]
links: []
created: 2026-03-17T06:49:18Z
type: feature
priority: 1
assignee: KMD
tags: [editor, prefab, bone, collider, viewport, hull]
---
# Prefab editor mode with bone collider parenting + hull markers

## Summary

Add a prefab editor mode activated by pressing P with a skeleton entity selected. This mode isolates the selected prefab (hides all other entities), enables adding collider primitives and parenting them to bones, and saves the result as a .fpfab prefab asset. Also adds convex hull support for bone colliders using markers to specify custom vertex point sets.

## Prefab Editor Mode (P Key)

### Activation
- Select an entity with a bound skeleton, press P
- The viewport switches to "prefab editor mode" for the selected entity
- All other scene entities are hidden
- The prefab entity renders normally with its skeleton bones visible
- Status bar shows "PREFAB: <entity_name>" or "PREFAB: humanoid.fpfab"
- Press P again (or Escape) to exit prefab mode and return to normal scene view

### Prefab Scope
- In prefab mode, the user can:
  - Add collider primitives (from ticket rpg-z9db) and parent them to specific bones
  - Transform colliders relative to bone space
  - Set collider properties (shape, size, mass, collision group)
  - Add markers for convex hull point specification
  - View and edit bone properties
- All changes apply to the prefab asset, NOT the scene entity
- The prefab represents the entity's collision/physics setup independently of its scene placement

### Tab Splitting
- Alt+Arrow splits the current viewport tab (existing BSP-based viewport tiling system)
- When splitting a tab that's in prefab mode, the new tab:
  - Shows the SAME prefab being edited (shared state)
  - Has INDEPENDENT viewport controls (camera position, angle, zoom)
  - Changes in either tab are immediately visible in the other
- Switching to a tab in prefab mode changes the inspector context to that tab's prefab

### Inspector Context
- When a tab in prefab mode is focused, the inspector shows:
  - Prefab name and asset path
  - Bone hierarchy with collider assignments
  - Selected collider properties (shape params, mass, collision group, bone binding)
  - Marker positions (for hull colliders)

### Parenting Colliders to Bones
- Collider entities created in prefab mode can be parented to a specific bone
- Parent relationship: collider transform is relative to the bone's rest pose
- When the bone moves (animation, ragdoll), the collider follows
- UI: select collider, then right-click a bone → "Parent to Bone" or drag-drop in outliner
- Multiple colliders can be parented to the same bone (compound collider)
- The outliner in prefab mode shows: bone hierarchy → colliders under each bone

### Saving
- Prefab mode saves to `.fpfab` files (prefab asset format)
- Auto-save on exit from prefab mode (with confirmation prompt)
- The .fpfab stores:
  - Reference to parent skeleton (.fskel path)
  - Per-bone collider assignments (shape type, params, transforms relative to bone)
  - Hull vertex data (from marker positions)
  - Collision group assignments
- `SCRIPT_KEY_PREFAB_PATH` attr on entities references the .fpfab

## Convex Hull Bone Colliders via Markers

### Marker-Based Hull Definition
- In prefab mode, add MARKER entities to define hull vertex positions
- Group markers by bone: parent markers to a bone, all markers under a bone define that bone's convex hull
- When the user parents 4+ markers to a bone and sets the bone's collider type to "convex hull":
  - The editor runs `phys_convex_hull_build()` on the marker positions
  - The hull wireframe is displayed in the collision overlay
  - The hull vertex data is stored in the prefab

### Workflow
1. Enter prefab mode (P) on a skeleton entity
2. Select a bone, set collider type to "convex hull" in inspector
3. Spawn markers at desired hull vertex positions (snap to mesh surface if needed)
4. Parent each marker to the bone (drag in outliner or right-click → Parent to Bone)
5. Hull wireframe updates live as markers are added/moved
6. Save prefab (exit prefab mode or manual save)

### Hull Regeneration
- Moving a marker updates the hull in real-time
- Adding/removing markers triggers hull rebuild
- Minimum 4 markers required for a valid hull (tetrahedron)
- Hull vertex count displayed in inspector; warning if > 64 (PHYS_CONVEX_MAX_VERTS)

## Key Files
- NEW: `include/ferrum/editor/scene/scene_prefab_mode.h` — prefab mode state
- NEW: `src/editor/scene/scene_prefab_mode.c` — enter/exit/update prefab mode
- NEW: `src/editor/scene/scene_prefab_save.c` — .fpfab serialization
- NEW: `src/editor/scene/scene_prefab_load.c` — .fpfab deserialization
- NEW: `include/ferrum/editor/prefab/prefab_def.h` — prefab definition types
- MODIFY: `src/editor/scene/scene_input.c` — P key binding for prefab mode
- MODIFY: `src/editor/scene/scene_viewport_draw.c` — hide/show entities based on prefab mode
- MODIFY: `src/editor/scene/scene_ui_inspector.c` — prefab/bone inspector context
- MODIFY: `src/editor/scene/scene_ui_outliner.c` — prefab bone+collider hierarchy
- MODIFY: `include/ferrum/entity/entity_attrs.h` — SCRIPT_KEY_PREFAB_PATH
- MODIFY: `src/editor/scene/scene_viewport_collision_overlay.c` — render hull from markers

## Acceptance Criteria
- [ ] P key with skeleton entity selected enters prefab mode; all other entities hidden
- [ ] Can spawn collider primitives (box/sphere/capsule) and parent them to bones
- [ ] Collider wireframes render correctly relative to bone positions
- [ ] Alt+Arrow splits viewport; both tabs show same prefab with independent cameras
- [ ] Tab focus switches inspector context to the active prefab
- [ ] Spawning 4+ markers under a bone with hull collider type generates and displays convex hull
- [ ] Moving markers updates hull wireframe in real-time
- [ ] Saving produces .fpfab file with all collider/hull data
- [ ] Loading .fpfab restores all bone collider assignments
- [ ] Escape or P exits prefab mode, restoring normal scene view
- [ ] Undo/redo works for all prefab mode operations

