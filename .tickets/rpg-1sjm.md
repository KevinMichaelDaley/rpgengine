---
id: rpg-1sjm
status: open
deps: [rpg-dzgo]
links: []
created: 2026-03-12T06:48:51Z
type: task
priority: 2
assignee: KMD
parent: rpg-rkj9
---
# §1.4 Object Mode

See ref/scene_editor_design.md §1.4. Left-click select (raycast), shift-click add/remove, box select. A/Shift+A select/deselect all. X/Delete for delete. D for duplicate. H/Shift+H hide/show. Ctrl+right-click 3D cursor. Transform gizmo interaction. Pivot manipulation (Alt+G, :pivot commands).

## Grid Snap Modes

- **Position snap**: quantize to grid spacing (per-axis on/off)
- **Rotation snap**: quantize to angle increment
- **Scale snap**: quantize to scale increment
- **Snap to face**: snap object to nearest face, orientation derived from face normal
- **Snap to vertex**: snap object to nearest vertex, orientation derived from vertex normal
- **Snap on surface**: like snap-to-face/vertex but offset along normal so object rests on surface without collision

Constraint-aware snap modes (snap-to-constraint, constrained movement) moved to rpg-pymj — depends on editor constraint system (rpg-nnfd).

## Cursor Transform Mode

The 3D cursor can be transformed using the same gizmo as entities. When in cursor transform mode, the active gizmo (translate/rotate/scale) applies to the cursor position/orientation instead of the selection. All snap modes apply identically. This allows precise cursor placement for use as a transform pivot or spawn location.

- Toggle via keybind or TUI command (e.g. `cursor transform`)
- Gizmo renders at cursor position with cursor orientation
- Drag applies transforms to cursor, not selection
- Snapping works the same as for entities
- Exiting cursor transform mode returns gizmo control to selection

## Acceptance Criteria

All object mode operations work: select, transform, duplicate, hide/show, snap, pivot, cursor transform. All snap modes produce correct absolute transforms sent to server. Cross-reference with ref/scene_editor_spec.md and ref/scene_editor_ux.md for requirements and interaction details.

## Notes

**2026-03-14T00:00:00Z**

Scope change: Removed G/R/S keyboard transforms — existing gizmo modes are sufficient. Expanded grid snap section with snap-to-face, snap-to-vertex, snap-on-surface, and snap-to-constraint modes. All snap operations derive absolute position/orientation locally and send to server.

**2026-03-15T00:00:00Z**

Moved snap-to-constraint and constrained movement to rpg-pymj (depends on rpg-nnfd constraint system). Added cursor transform mode: gizmo applies to 3D cursor with same snap options.
