---
id: rpg-isf5
status: closed
deps: []
links: []
created: 2026-03-15T08:41:19Z
type: feature
priority: 2
assignee: KMD
tags: [editor, viewport, gizmo]
---
# Per-object gizmo mode with independent transforms

## Overview

Add a toggleable mode where each selected entity gets its own independent gizmo, rather than a single shared gizmo at the selection center. The user picks whichever gizmo they want to interact with and transforms only that entity.

## Current behavior

- One gizmo per selection (positioned at average of selected entity positions, or at 3D cursor in cursor basis)
- All selected entities transform together when the gizmo is dragged
- No way to transform individual entities within a multi-selection without deselecting the others

## Desired behavior

### Toggle
- Keybinding (e.g. T or a toolbar button) toggles between:
  - **Unified mode** (current): single gizmo, transforms all selected entities
  - **Per-object mode** (new): one gizmo per selected entity, transforms only the picked entity

### Per-object mode details

1. **Rendering**: Draw a gizmo at each selected entity's \`pos\` (pivot position). Each gizmo uses the entity's own orientation for local-basis, or world axes for global-basis.

2. **Picking**: On mouse down, hit-test all per-object gizmos. The closest hit determines which entity + axis is being dragged. Only that entity transforms during the drag.

3. **Commands**: On drag end, send a single \`move_id\`/\`rotate_id\`/\`scale_id\` command for the dragged entity (not a bulk \`move\`/\`rotate\`/\`scale\`).

4. **Visual feedback**: The actively-dragged gizmo highlights normally. Non-dragged gizmos remain visible but dimmed/desaturated.

5. **Gizmo scale**: Each gizmo is screen-size-corrected independently based on its own distance to camera.

6. **Snap**: Snap applies per-entity (using that entity's position/orientation as the snap origin).

## Implementation notes

### Viewport state
- Add \`bool per_object_gizmo\` to \`viewport_state_t\`
- In per-object mode, skip the shared gizmo position/orientation computation in \`scene_viewport_draw.c\`

### Gizmo rendering
- \`viewport_render_draw_gizmo\` currently takes a single \`gizmo_state_t\`. For per-object mode, call it in a loop over selected entities, each with a temporary \`gizmo_state_t\` positioned at that entity's \`pos\` with that entity's orientation.

### Gizmo picking
- \`gizmo_hit_test\` currently tests one gizmo. For per-object mode, iterate all selected entities' gizmos and pick the nearest hit. Store the picked entity ID in the viewport state so the drag applies only to it.

### Drag application
- \`apply_gizmo_drag\` / \`apply_gizmo_rotate\`: in per-object mode, only modify the single picked entity instead of iterating all selected.
- \`send_gizmo_commands\`: in per-object mode, send \`move_id\`/\`rotate_id\`/\`scale_id\` for the single entity.

### Edge cases
- If the selection changes while in per-object mode, gizmos update immediately
- If selection is empty, no gizmos are drawn (same as unified mode)
- Pivot edit mode should remain single-entity only (no interaction with per-object mode)
- Cursor basis in per-object mode: each gizmo is still at the entity's pos, not the cursor

## Files likely affected

| File | Change |
|------|--------|
| \`include/ferrum/editor/scene/viewport_bsp/viewport_state.h\` | Add \`per_object_gizmo\` flag, \`per_object_drag_entity\` ID |
| \`src/editor/scene/scene_viewport_draw.c\` | Per-object gizmo rendering loop |
| \`src/editor/scene/scene_viewport_gizmo.c\` | Per-object gizmo hit test |
| \`src/editor/scene/scene_input.c\` | Toggle keybinding, per-object drag/send logic |

## Acceptance criteria

- Keybinding toggles between unified and per-object gizmo modes
- In per-object mode, each selected entity shows its own gizmo
- Dragging one gizmo transforms only that entity
- Server receives per-entity commands (move_id/rotate_id/scale_id)
- Snap works per-entity in per-object mode
- Switching back to unified mode restores current behavior exactly
- No visual or behavioral regression in unified mode

