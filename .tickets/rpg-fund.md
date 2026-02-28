---
id: rpg-fund
status: open
deps: [rpg-clq6]
links: []
created: 2026-02-28T22:22:46Z
type: task
priority: 2
assignee: KMD
parent: rpg-9vcc
tags: [editor, mesh, topology]
---
# Extrude and extrude_individual

Implement face extrusion — the most critical mesh modeling operation.

extrude: Duplicate selected faces, offset along face normals (or specified direction), and create side walls connecting original edges to new edges.
Algorithm:
1. For each selected face, duplicate its vertices
2. Offset duplicated vertices by distance along normal (or direction vector)
3. Create quad side walls between each original boundary edge and its duplicate
4. Triangulate side wall quads (2 triangles each)
5. Remove original faces (replaced by new offset faces)
6. Update normals for new and side-wall faces

extrude_individual: Same as extrude but each face is extruded independently (no merging of shared edges between selected faces). Creates separate pillars for each face.

Args:
- extrude: {"distance": float, "direction": [x,y,z] OR "normal": true, "segments": int}
- extrude_individual: {"distance": float}

Files to create:
- include/ferrum/editor/mesh/mesh_extrude.h — extrude API
- src/editor/mesh/mesh_extrude.c — shared extrude logic (face duplication, wall creation)
- src/editor/mesh/mesh_extrude_individual.c — individual face extrude variant
- src/editor/commands/cmd_extrude.c — command handlers
- tests/editor/mesh_extrude_tests.c

## Acceptance Criteria

- Extrude single face of box: 6 faces → 10 faces (1 top + 4 walls + 5 original)
- Extrude preserves UV coordinates on extruded faces
- Side walls have correct normals (outward-facing)
- Extrude with segments > 1 creates intermediate rings
- extrude_individual on 2 adjacent faces creates 2 separate pillars
- Empty selection: no-op, returns false
- Direction override: extrude along arbitrary vector instead of normal
- All indices valid, no degenerate triangles
- Tests: single face, multi face, individual, segments, direction, empty

