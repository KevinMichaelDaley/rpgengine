---
id: rpg-6fi0
status: closed
deps: [rpg-9vcc]
links: []
created: 2026-02-28T22:20:09Z
type: epic
priority: 1
assignee: KMD
tags: [editor, mesh, uv, materials]
---
# Phase 3: UV Mapping & Materials

UV mapping and per-face material assignment for the mesh modeling mode. Implements UV projection methods (planar, box, cylindrical, spherical), smart unwrap, UV seam marking, UV transform commands, UV packing, per-face material assignment, material sampling, and texture flow across connected faces.

READ FIRST: ref/mesh_modeling_spec.md §UV Mapping Commands, §Material Commands

UV data is stored per-vertex in the mesh_slot_t uv channels (uv0, uv1). UV seams are marked on edges. Per-face material assignment uses polygroup IDs to map face groups to material paths.

Key considerations:
- UV projection must handle arbitrary face selections, not just entire meshes
- Smart unwrap minimizes stretch using angle-based flattening (ABF)
- Seam edges split UV coordinates without splitting geometry
- Material assignment is per-face (stored as polygroup → material mapping)
- Texel density tools ensure consistent pixel-per-unit ratios

