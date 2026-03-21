---
id: rpg-nf08
status: closed
deps: []
links: []
created: 2026-03-17T09:02:22Z
type: feature
priority: 1
assignee: KMD
---
# Prefab child spawning, skeletal mesh promotion, asset reference widget, bone gizmos

## Summary

Add the ability to populate prefab entity trees from the asset browser, convert static meshes to skeletal meshes via the inspector, and manipulate skeleton bones with gizmos in prefab mode.

## Components

### 1. Asset Reference Selector Widget (generic, reusable)
A new Clay UI widget type for selecting assets by path. Used in the inspector and potentially anywhere else assets need to be referenced.
- Text input box showing current asset path (editable)
- When focused, clicking an asset in the asset tree fills the text input
- Pressing Enter or clicking a filled-left-arrow shaped confirm button accepts the selection
- Generic: parameterized by asset type filter (mesh, skeleton, material, etc.)
- Stores result path in a provided buffer

### 2. Inspector: Static Mesh → Skeletal Mesh Conversion
- MESH entities get a gear icon button in the inspector title row (top-right corner)
- Clicking the gear opens a "Skeleton" section below the transform properties
- The skeleton section contains an asset reference selector filtered to EDIT_ASSET_SKELETON
- Selecting a skeleton promotes the entity to skeletal mesh (calls scene_load_entity_skeleton)
- Stores the skeleton path in SCRIPT_KEY_SKEL_PATH attr on the entity

### 3. Asset Tree → Prefab Child Spawning
- When prefab mode is active, clicking an asset in the asset tree spawns a child entity parented to the prefab root
- The new entity gets SCRIPT_KEY_PARENT_ID set to the prefab root entity ID
- Works for all asset types that can spawn entities (meshes, primitives)
- The prefab_mode.dirty flag is set to true

### 4. Skeleton Display in Prefab Mode Viewports
- When the prefab root entity has a SCRIPT_KEY_SKEL_PATH attr, load and display the skeleton
- Bones rendered as long capsules from head to tail (using existing bone_capsule_params system)
- Skeleton moves with the root entity's transform

### 5. Bone Manipulation with Gizmos
- T toggles per-object gizmo mode (already exists) — works for bone selection in prefab mode
- Selected bones get translate/rotate gizmos
- Bone selection via click-on-capsule (existing bone_pick system)
- Gizmo transforms adjust the bone's local transform within the skeleton

## Dependencies
- rpg-kte9 (prefab mode) — closed
- Existing systems: bone overlay, bone picking, per-object gizmos, asset browser, inspector

