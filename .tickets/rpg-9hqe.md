---
id: rpg-9hqe
status: open
deps: [rpg-fund]
links: []
created: 2026-02-28T22:22:59Z
type: task
priority: 2
assignee: KMD
parent: rpg-9vcc
tags: [editor, mesh, topology]
---
# Inset and outset

Implement face inset and outset operations.

inset: Scale selected faces inward by 'amount', creating a border ring of new faces between the original edges and the inset edges. Optional 'depth' parameter offsets the inset face along its normal (like a shallow extrude).
Algorithm:
1. For each selected face, create a new smaller face centered on the original
2. Each original edge gets a quad (2 tris) connecting it to the corresponding inset edge
3. If depth != 0, offset inset vertices along face normal

outset: Push selected faces outward along their normals by 'amount'. Unlike extrude, outset scales the face perimeter outward rather than duplicating it.

Args:
- inset: {"amount": float, "depth": float}
- outset: {"amount": float}

Files to create:
- src/editor/mesh/mesh_inset.c — inset algorithm
- src/editor/mesh/mesh_outset.c — outset algorithm
- src/editor/commands/cmd_inset.c — command handlers
- tests/editor/mesh_inset_tests.c

## Acceptance Criteria

- Inset single face: creates border ring (4 quads = 8 tris for a quad face)
- Inset with depth: inset face offset along normal
- Inset amount=0: no-op
- Outset moves face vertices outward along averaged normal
- Border faces have correct normals and UVs
- Multiple selected faces: each inset independently
- Tests: single face inset, depth, outset, multiple faces, zero amount

