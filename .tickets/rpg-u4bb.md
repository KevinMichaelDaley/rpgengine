---
id: rpg-u4bb
status: open
deps: []
links: [rpg-rt38]
created: 2026-07-09T03:25:23Z
type: task
priority: 2
assignee: KMD
parent: rpg-pm1c
tags: [arch, procgen, blender, bpy, mesh]
---
# Arch mesh: ceiling beam and coffer panel (bpy/bmesh)

Parametric-mesh port of `rpg-rt38` (SDF rewrite rules: ceiling beams and vault
ribs) to standalone `bpy`/`bmesh` generators under `assets/arch/proc/`. Ceiling
structure. Visible in mead hall (exposed beams), great hall / crypt (vault ribs),
forge (beam grid).

Each generator is standalone — a few parameters in, one piece out. This ticket
describes **what the piece should look like and its parameters**, not how to build
it. Beam rows and coffered grids are assembled by placing/instancing in the editor.
We will iterate on the `bmesh` implementation.

## Pieces

- **beam** — a single straight ceiling beam hanging below the ceiling plane.
  Params: length, width, drop (depth below ceiling), and optional bottom chamfer.
  Reads as an exposed timber / stone beam; tiles into parallel rows.
- **coffer_panel** — a single coffered ceiling tile: a square recessed panel framed
  by beam-like ribs. Params: size, rib width, coffer (recess) depth. Tiles into a
  grid to form a coffered / waffle ceiling.

## Files
- `assets/arch/proc/beam.py`

## Acceptance Criteria
- Runnable through the Blender MCP bridge, producing a viewable mesh per piece
- beam reads as a straight member hanging below the ceiling; drop follows param
- coffer_panel reads as a framed recessed tile that tiles into a coffered grid
- Pieces tile cleanly edge-to-edge
- Output geometry is manifold with sane normals

