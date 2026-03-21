---
id: rpg-b7bc
status: open
deps: [rpg-haqg, rpg-2585]
links: [rpg-c6o7, rpg-nnfd]
created: 2026-03-21T07:00:00Z
type: task
priority: 2
assignee: kmd
parent: rpg-c6o7
---
# §5.2.3 Constraint anchor system

Constraint anchors are attachment points on bones where physics joints connect. Each bone can have multiple anchors positioned and oriented in bone-local space.

## Anchor Properties

- **Position** — local-space offset from bone head (3D vector)
- **Orientation** — local-space rotation (quaternion / euler) defining the constraint axis
- **Name** — optional label (e.g., "shoulder_front", "elbow_hinge")
- **Bone index** — which bone this anchor belongs to

## Anchor Gizmos

Anchors have their own independent gizmos, separate from the bone gizmo:

- Rendered as small diamond/cross markers at the anchor's world position
- **Selectable** — click an anchor in the viewport to select it (like clicking a pivot)
- **Movable** — translate gizmo repositions the anchor in bone-local space
- **Rotatable** — rotation gizmo orients the anchor (defines constraint axis for hinges, etc.)
- Gizmo respects world/local basis toggle
- Multiple anchors on the same bone render as distinct markers; clicking nearest one selects it
- Selected anchor highlighted (yellow vs grey for unselected)
- Unselected anchors render smaller/dimmer to avoid clutter

## Workflow

1. Select a bone in the outliner or viewport
2. Inspector shows "Anchors" section with list of existing anchors
3. Click "Add Anchor" → new anchor at bone head with identity orientation
4. Select the anchor → gizmo appears → drag to position, rotate to orient
5. When creating a joint, pick two anchors (one per bone) as endpoints

## Commands

- `anchor_add <bone_id> [x y z]` — add anchor at position (default: bone head)
- `anchor_delete <bone_id> <anchor_index>` — remove anchor
- `anchor_list [bone_id]` — list anchors with positions

## Storage

- Per-bone array of anchor descriptors stored in .fskel (v3 ANCH chunk) or .fpfab
- Serialized alongside bone_collider_desc_t
- Undo/redo for anchor create/delete/move/rotate

## Key files

- NEW: include/ferrum/editor/edit_anchor.h — anchor descriptor type
- NEW: src/editor/state/edit_anchor_store.c — anchor storage and lifecycle
- src/editor/scene/scene_ui_bone_inspector.c — anchor list in inspector
- src/editor/scene/scene_viewport_overlay.c — anchor marker rendering
- src/editor/scene/scene_input.c — anchor picking and gizmo interaction
