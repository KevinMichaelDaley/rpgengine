---
id: rpg-n0dl
status: open
deps: [rpg-ivbg]
links: []
created: 2026-02-28T22:25:24Z
type: task
priority: 2
assignee: KMD
parent: rpg-6fi0
tags: [editor, mesh, uv]
---
# UV transform commands (shift, rotate, scale, fit)

Implement UV-space transform commands for fine-tuning texture alignment.

transform_uv: General UV transform with offset, scale, and rotation.
shift_uv: Translate UV coordinates by offset.
rotate_uv: Rotate UVs around a pivot point.
scale_uv: Scale UVs around a pivot point.
fit_uv: Scale UV island to fit within 0-1 space (per axis or both).
align_uv_to_grid: Snap UV coordinates to a grid (for texel alignment).

All operations apply to UVs of selected faces only. The pivot defaults to the UV centroid of the selection.

Args:
- transform_uv: {"offset": [u,v], "scale": [su,sv], "rotate": float}
- shift_uv: {"offset": [u,v]}
- rotate_uv: {"angle": float, "pivot": [u,v]}
- scale_uv: {"factors": [su,sv], "pivot": [u,v]}
- fit_uv: {"axis": "x"|"y"|"both"}
- align_uv_to_grid: {"grid_size": float}

Files to create:
- src/editor/mesh/mesh_uv_transform.c — shift, rotate, scale
- src/editor/mesh/mesh_uv_fit.c — fit and grid alignment
- src/editor/commands/cmd_uv_transform.c — command handlers
- tests/editor/mesh_uv_transform_tests.c

## Acceptance Criteria

- shift_uv offsets all selected UVs by the given amount
- rotate_uv rotates around pivot point (default: centroid)
- scale_uv scales relative to pivot
- fit_uv normalizes UVs to [0,1] range
- align_uv_to_grid snaps to nearest grid point
- Unselected face UVs unchanged
- Tests: shift, rotate 90°, scale 2x, fit, grid snap, partial selection

