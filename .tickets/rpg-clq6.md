---
id: rpg-clq6
status: closed
deps: [rpg-tze6]
links: []
created: 2026-02-28T22:21:49Z
type: task
priority: 2
assignee: KMD
parent: rpg-caw8
tags: [editor, mesh, selection]
---
# Mesh element selection commands

Extend existing selection commands with mesh element selection capability, plus add new mesh-specific selection commands.

Extended commands (add scope='mesh' support):
- select: add by indices, boundary edges, planar faces (normal+threshold)
- deselect: remove by indices
- select_all: select all elements in current mode
- select_none / deselect_all: clear mesh selection
- invert_selection: flip current selection

New mesh-specific commands:
- select_ring: select edge ring (parallel edges around a loop)
- select_loop: select edge loop (continuous ring of edges)
- select_flood: flood fill select connected faces from a seed face
- select_similar: select elements by similarity (normal, area, polygroup)
- grow_selection: expand selection by N rings of adjacency
- shrink_selection: contract selection by N rings

All commands operate on the current selection mode's element type. The scope='mesh' arg distinguishes mesh selection from entity selection.

Files to create:
- src/editor/mesh/mesh_select.c — select by indices, select all, clear, invert
- src/editor/mesh/mesh_select_topo.c — ring, loop, flood, similar
- src/editor/mesh/mesh_select_grow.c — grow, shrink
- src/editor/commands/cmd_mesh_select.c — command handlers
- tests/editor/mesh_select_tests.c

## Acceptance Criteria

- select by indices correctly sets bitset bits
- select_all sets all bits for current mode
- invert_selection flips all bits
- select_flood from seed face reaches all connected faces, stops at boundaries
- select_ring selects parallel edges (quad strip detection)
- select_loop selects continuous edge loop
- grow_selection(1) expands by one ring of adjacency
- shrink_selection(1) contracts by one ring
- select_similar by normal selects faces within angle threshold
- All commands work with scope='mesh' argument
- Entity selection (no scope arg) unchanged
- Tests: indices, all, invert, flood, ring, loop, grow, shrink, similar, boundary

