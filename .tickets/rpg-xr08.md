---
id: rpg-xr08
status: open
deps: []
links: [rpg-qhya]
created: 2026-07-09T03:17:06Z
type: task
priority: 1
assignee: KMD
parent: rpg-pm1c
tags: [arch, procgen, blender, bpy, mesh]
---
# Arch mesh: wall ledge and cornice (bpy/bmesh)

Parametric-mesh port of `rpg-qhya` (SDF rewrite rules: wall ledge and cornice) to
standalone `bpy`/`bmesh` generators under `assets/arch/proc/`. Horizontal molding
runs that band a wall and give it a clear horizontal rhythm. Visible in library
(arch-spring string course), council chamber (gallery ledge), guard checkpoint,
mine depot (platform edges).

Each generator is standalone — a few parameters in, one straight run out. This
ticket describes **what the piece should look like and its parameters**, not how to
build it. We will iterate on the `bmesh` implementation.

## Pieces

- **wall_ledge** — a straight run of a shelf / string-course molding that projects
  from a wall plane. Params: length, projection (depth off the wall), height
  (band thickness), and profile style (flat shelf vs chamfered). The back sits
  flush against the wall.
- **ceiling_cornice** — a straight run of an L-shaped cornice molding that seats
  into the corner where a wall meets the ceiling. Params: length, wall-leg depth,
  ceiling-leg depth, profile style. Intended to be run around a room's top edge
  and mitered at corners during placement.

## Files
- `assets/arch/proc/cornice.py`

## Acceptance Criteria
- Runnable through the Blender MCP bridge, producing a viewable mesh per piece
- wall_ledge reads as a horizontal shelf molding with a flush back
- ceiling_cornice reads as an L-section molding that seats into a wall/ceiling corner
- Straight runs are uniform along their length so segments tile end-to-end
- Output geometry is manifold with sane normals
