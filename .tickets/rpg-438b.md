---
id: rpg-438b
status: in_progress
deps: []
links: [rpg-2585, rpg-haqg, rpg-c6o7]
created: 2026-03-25T03:24:51Z
type: task
priority: 1
assignee: kmd
---
# Armature as independent entity (not parented to mesh)

The skeleton/armature must be its own entity in the scene with an independent transform (position, rotation, scale), NOT parented to or slaved to the mesh entity. This matches the standard 3D software pattern (Blender, Maya, etc.) where the armature is a sibling object that can be positioned, rotated, and scaled independently to align with the mesh.

Currently, bone overlays render using the mesh entity's model matrix (`build_model_matrix(entity)`), meaning the skeleton is locked to the mesh's transform with no way to adjust alignment.

## Deliverables

### New Entity Type: EDIT_ENTITY_TYPE_ARMATURE
- A new entity type specifically for armatures
- Has its own pos/rot/scale transform, selectable and movable with gizmos
- Stores SCRIPT_KEY_SKEL_PATH attribute pointing to the .fskel file
- Does NOT render a mesh — only the bone overlay
- Visible in the outliner as a distinct type with "[armature]" tag

### Skeleton Binding
- A mesh entity references an armature entity (not the .fskel directly)
- New attribute: SCRIPT_KEY_ARMATURE_ID (u32) on mesh entities pointing to the armature entity
- The mesh entity's skinning pipeline reads bone transforms from the armature entity's skeleton
- Multiple mesh entities can share the same armature (e.g., body + clothing)

### Bone Overlay Rendering
- Bone overlay renders using the ARMATURE entity's model matrix, not the mesh entity's
- When the armature is selected, its bones are shown in the outliner
- Bone selection, gizmo transforms, and undo all operate on the armature entity

### Skeleton Mode
- K key with an armature entity selected enters skeleton mode for that armature's .fskel
- K key with a mesh entity selected enters skeleton mode for its bound armature's .fskel
- Skeleton mode renders the armature at its own transform (not mesh's)

### Spawning/Workflow
- When loading a mesh that has bone weights, automatically spawn an armature entity at the same position
- The armature and mesh are siblings (not parent-child) in the LCRS tree
- User can move/rotate the armature independently to fix alignment
- Inspector shows "Armature: <name>" field on mesh entities with a picker

### Migration
- Existing entities with SCRIPT_KEY_SKEL_PATH that are NOT armature type:
  - On load, automatically create a companion armature entity
  - Transfer the SKEL_PATH to the armature
  - Set ARMATURE_ID on the mesh entity

## Acceptance Criteria

- [ ] EDIT_ENTITY_TYPE_ARMATURE exists and is spawnable
- [ ] Armature entity has independent pos/rot/scale with gizmos
- [ ] Bone overlay renders at armature's transform, not mesh's
- [ ] Mesh entity references armature via ARMATURE_ID attribute
- [ ] Moving the armature moves bones independently of the mesh
- [ ] Skeleton mode works from armature entity selection
- [ ] Outliner shows armature as "[armature]" type
- [ ] Multiple meshes can share one armature
- [ ] Existing skeleton-bearing entities auto-migrate on load
