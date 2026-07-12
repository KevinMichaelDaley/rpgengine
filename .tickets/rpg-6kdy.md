---
id: rpg-6kdy
status: open
deps: []
links: [rpg-z40l]
created: 2026-07-09T03:25:22Z
type: task
priority: 2
assignee: KMD
parent: rpg-pm1c
tags: [arch, procgen, blender, bpy, mesh]
---
# Arch mesh: circular basin and channel (bpy/bmesh)

Parametric-mesh port of `rpg-z40l` (SDF rewrite rules: circular floor pits and
channels) to standalone `bpy`/`bmesh` generators under `assets/arch/proc/`. Sunken
floor features. Visible in smelting hall (crucible pits + channels), bathhouse
(basins), cistern (central reservoir).

Each generator is standalone — a few parameters in, one piece out. This ticket
describes **what the piece should look like and its parameters**, not how to build
it. Grids of pits are assembled by placing/instancing in the editor. We will
iterate on the `bmesh` implementation.

## Pieces

- **basin** — a circular sunken pit / tub. Params: radius, depth, wall thickness,
  and bottom profile (flat vs bowl). Reads as a round crucible / bathing basin
  recessed below a floor plane.
- **channel** — a straight trough segment. Params: length, width, depth, and
  cross-section (square vs rounded). Tiles end-to-end to link basins.

## Files
- `assets/arch/proc/basin.py`

## Acceptance Criteria
- Runnable through the Blender MCP bridge, producing a viewable mesh per piece
- basin reads as a round sunken tub; bottom profile toggles flat vs bowl
- channel reads as a straight trough that tiles end-to-end
- Params scale radius/depth/width sensibly
- Output geometry is manifold with sane normals

