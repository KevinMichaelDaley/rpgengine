---
id: rpg-umku
status: closed
deps: []
links: [rpg-t9l2]
created: 2026-07-09T03:24:53Z
type: task
priority: 1
assignee: KMD
parent: rpg-pm1c
tags: [arch, procgen, blender, bpy, mesh]
---
# Arch mesh: column with base and capital (bpy/bmesh)

Parametric-mesh port of `rpg-t9l2` (SDF rewrite rules: column grid with bases and
capitals) to a standalone `bpy`/`bmesh` generator under `assets/arch/proc/`.
A proper column with base + shaft + capital. Visible in undercroft, treasury, mead
hall, cistern. Grids and rows are assembled by placing/instancing this piece in
the editor.

Each generator is standalone — a few parameters in, one piece out. This ticket
describes **what the piece should look like and its parameters**, not how to build
it. We will iterate on the `bmesh` implementation.

## Pieces

- **column** — a single freestanding column reading as base pedestal + cylindrical
  shaft + wider capital, stacked bottom to top. Params: total height, shaft radius,
  base size + base height, capital size + capital height, and shaft sides (round
  vs polygonal). Origin at the base center.

## Files
- `assets/arch/proc/column.py`

## Acceptance Criteria
- Runnable through the Blender MCP bridge, producing a viewable mesh
- column reads as base + shaft + capital with the base and capital wider than the
  shaft
- Params scale height/radius/base/capital independently and sensibly
- Origin at base center so instances place cleanly on a floor
- Output geometry is manifold with sane normals

