---
id: rpg-elky
status: open
deps: [rpg-nulp]
links: []
created: 2026-02-28T22:26:54Z
type: task
priority: 2
assignee: KMD
parent: rpg-37uq
tags: [editor, mesh, io]
---
# Mesh data transfer (import, export, copy, paste, clear)

Implement mesh data transfer commands for clipboard operations and file I/O.

mesh_import: Load an external mesh file (glTF, OBJ) into a mesh slot. Parse the file format and populate mesh_slot_t buffers.

mesh_export: Save current mesh to a file (glTF, OBJ). Serialize mesh_slot_t to the specified format.

mesh_copy: Copy mesh data from one slot to another (deep copy). Used for clipboard: copy active to @scratch.

mesh_paste: Merge mesh from one slot into another. Used for clipboard: paste @scratch into @active.

mesh_clear: Empty a mesh slot (zero out all buffers).

Args:
- mesh_import: {"path": string, "slot": int}
- mesh_export: {"path": string, "format": "gltf"|"obj"}
- mesh_copy: {"from_slot": int, "to_slot": int}
- mesh_paste: {"from_slot": int, "merge_mode": "append"|"replace"}
- mesh_clear: {"slot": int}

Files to create:
- src/editor/mesh/mesh_import_obj.c — OBJ file parser
- src/editor/mesh/mesh_export_obj.c — OBJ file writer
- src/editor/mesh/mesh_clipboard.c — copy, paste, clear
- src/editor/commands/cmd_mesh_transfer.c — command handlers
- tests/editor/mesh_transfer_tests.c

## Acceptance Criteria

- mesh_copy produces identical mesh in target slot
- mesh_paste appends geometry (vertex indices offset correctly)
- mesh_clear zeros all buffers and counts
- OBJ import: vertices, normals, UVs, faces parsed correctly
- OBJ export: produces valid OBJ file readable by external tools
- Round-trip: export then import produces equivalent mesh
- Tests: copy, paste, clear, OBJ import, OBJ export, round-trip

