---
id: rpg-k04n
status: open
deps: []
links: [rpg-goao]
created: 2026-07-09T03:17:06Z
type: task
priority: 1
assignee: KMD
parent: rpg-pm1c
tags: [arch, procgen, blender, bpy, mesh]
---
# Arch mesh: partition wall and arcade (bpy/bmesh)

Parametric-mesh port of `rpg-goao` (SDF rewrite rules: partition walls and arcade
walls) to standalone `bpy`/`bmesh` generators under `assets/arch/proc/`. Wall
panels and see-through arcade runs used to subdivide space. Visible in prison
block (solid dividers), guard checkpoint (arcade with arched openings), bathhouse,
barracks.

Each generator is standalone — a few parameters in, one piece out. This ticket
describes **what the piece should look like and its parameters**, not how to build
it. We will iterate on the `bmesh` implementation.

## Pieces

- **partition_wall** — a freestanding wall panel, floor to ceiling. Params:
  length, height, thickness, and optional doorway opening (position + width) cut
  through it. Reads as a plain dividing wall, with a passage where specified.
- **arcade** — a colonnade run: a row of piers carrying rounded arches, open
  between the piers (you can see through it). Params: bay count, bay width, height,
  arch rise (curvature), pier width, depth. Reads as an arcade / open screen wall.

## Files
- `assets/arch/proc/partition.py`

## Acceptance Criteria
- Runnable through the Blender MCP bridge, producing a viewable mesh per piece
- partition_wall reads as a plain wall panel; the optional doorway is an actual
  through-opening
- arcade reads as a row of piers with rounded arches, open between the piers
- bay count / width scale the arcade run sensibly; ends are clean for tiling
- Output geometry is manifold with sane normals
