---
id: rpg-co02
status: closed
deps: [rpg-ivbg, rpg-n0dl]
links: []
created: 2026-02-28T22:25:35Z
type: task
priority: 2
assignee: KMD
parent: rpg-6fi0
tags: [editor, mesh, uv]
---
# UV packing and texel alignment

Implement UV island packing and texel density tools.

pack_uvs: Arrange UV islands within the 0-1 UV space to minimize wasted area. Uses a bin-packing algorithm with configurable padding between islands.

hotspot_apply: Apply a texel density hotspot — a named preset that sets a specific pixels-per-unit ratio. Ensures consistent texture resolution across the level.

Args:
- pack_uvs: {"padding": float, "resolution": int}
- hotspot_apply: {"hotspot_name": string}

Files to create:
- src/editor/mesh/mesh_uv_pack.c — UV island packing (bin-pack algorithm)
- src/editor/mesh/mesh_uv_density.c — texel density calculation and hotspot application
- src/editor/commands/cmd_uv_pack.c — command handlers
- tests/editor/mesh_uv_pack_tests.c

## Acceptance Criteria

- pack_uvs arranges islands within [0,1] with no overlap
- Padding between islands respects the padding parameter
- Resolution hint affects packing granularity
- Texel density correctly calculated as pixels per world unit
- Hotspot scales UVs to achieve target texel density
- Tests: single island pack, multi island, padding, density calc, hotspot

