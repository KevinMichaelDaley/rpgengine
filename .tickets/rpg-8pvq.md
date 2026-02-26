---
id: rpg-8pvq
status: open
deps: [rpg-00d1, rpg-ssj2, rpg-6auf]
links: []
created: 2026-02-26T04:26:35Z
type: task
priority: 1
assignee: KMD
parent: rpg-wxom
tags: [editor, foundation, server]
---
# Level save/load (JSON serialization)

Implement level serialization to/from JSON files.

READ FIRST: ref/editor_spec.md §2.6 for level serialization format (version, world_settings, entities array), ref/editor_design.md §12 for phased plan (save/load is Phase 1).

Requirements:
- cmd_save: serialize all entities to JSON file at specified path
- cmd_load: deserialize entities from JSON file, spawn them all
- Format per spec §2.6: version, world_settings (gravity, bounds), entities array with id/type/position/rotation/components
- Load clears current world before spawning (confirm prompt from controller)
- Save overwrites existing file (controller can prompt for confirmation)
- File path validation (no directory traversal attacks)
- Error handling: file not found, parse errors, version mismatch

Files to create:
- src/editor/commands/cmd_save.c
- src/editor/commands/cmd_load.c
- src/editor/state/edit_serialize.c
- tests/editor/edit_serialize_tests.c

Dependencies: json_parse, edit_dispatch, entity commands

