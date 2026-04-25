---
id: scene-006
status: open
deps: [scene-002, scene-004]
links: []
created: 2026-04-02T05:17:00Z
type: task
priority: 3
assignee: kmd
---
# Asset Browser: Materials

Display .fmat files in the asset browser panel with preview and apply capability.

## Features

- List .fmat files in assets/materials/ directory
- Show name, thumbnail (albedo preview if available)
- Double-click: load material into "current material" state
- Drag to entity: apply material to entity
- Right-click: context menu (apply to selected, edit, delete)

## Implementation

- Extend `src/editor/panels/asset_browser.c`
- Add material filter type
- Apply uses cmd_mat_apply internally

## Deliverables

- [ ] Asset browser shows .fmat files
- [ ] Double-click loads material
- [ ] Drag-drop applies to entity
- [ ] Thumbnail from albedo texture (optional)

## Acceptance Criteria

- [ ] .fmat files appear in asset browser
- [ ] Drag-drop onto entity applies material
- [ ] Applied material shows in viewport
