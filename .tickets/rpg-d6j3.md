---
id: rpg-d6j3
status: open
deps: []
links: []
created: 2026-02-26T04:27:43Z
type: task
priority: 2
assignee: KMD
parent: rpg-tiet
tags: [editor, assets, server]
---
# Asset registry (edit_asset_registry.c)

Implement the server-side asset registry that catalogs all available assets.

READ FIRST: ref/editor_spec.md §2.2 for asset registry spec, ref/editor_design.md §3 for asset download protocol.

Requirements:
- Scans configured asset directories on startup
- Maintains catalog: path, type (mesh/texture/prefab/script), size, hash
- Supports listing (by directory), searching (by name/type), and path completion
- Used by tab-completion (complete command) and browse command
- Thread-safe reads (catalog is built on startup, read-only during editing unless hot-reload later)
- Supports asset registration at runtime (when scripts create new assets)

Files to create:
- include/ferrum/editor/edit_asset_registry.h
- src/editor/assets/edit_asset_registry.c
- src/editor/assets/edit_asset_scan.c
- tests/editor/edit_asset_registry_tests.c

