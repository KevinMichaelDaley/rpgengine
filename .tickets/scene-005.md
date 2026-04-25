---
id: scene-005
status: open
deps: [scene-001, scene-003]
links: []
created: 2026-04-02T05:17:00Z
type: task
priority: 3
assignee: kmd
---
# Asset Browser: Entity Definitions

Display .fentity files in the asset browser panel with preview and spawn capability.

## Features

- List .fentity files in assets/entities/ directory
- Show name, type, mesh preview thumbnail
- Double-click: spawn entity in viewport at cursor
- Right-click: context menu (spawn, edit definition, delete)
- Drag to viewport: spawn at drop location

## Implementation

- Extend `src/editor/panels/asset_browser.c`
- Add entity definition filter type
- Spawn uses cmd_spawn_def internally

## Deliverables

- [ ] Asset browser shows .fentity files
- [ ] Double-click spawns entity
- [ ] Drag-drop spawn works
- [ ] Thumbnail/preview (optional, can be placeholder icon)

## Acceptance Criteria

- [ ] .fentity files appear in asset browser
- [ ] Double-click spawns entity at viewport center
- [ ] Spawned entity has all properties from definition
