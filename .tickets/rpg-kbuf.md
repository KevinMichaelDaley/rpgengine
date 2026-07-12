---
id: rpg-kbuf
status: open
deps: []
links: [rpg-ca3y]
created: 2026-07-09T03:17:06Z
type: task
priority: 1
assignee: KMD
parent: rpg-pm1c
tags: [arch, procgen, blender, bpy, mesh]
---
# Arch mesh: pilasters and buttresses (bpy/bmesh)

Parametric-mesh port of `rpg-ca3y` (SDF rewrite rules: pilasters and wall
buttresses) to standalone `bpy`/`bmesh` generators under `assets/arch/proc/`.
Wall-engaged piers that articulate an otherwise flat wall; visible in ~50% of
dwarven dataset rooms (shrine, library, grand gate hall, treasury, guard
checkpoint, mine depot).

Each generator is standalone — a few parameters in, one piece out. This ticket
describes **what the piece should look like and its parameters**, not how to build
it. Rows/spacing are a placement concern for the editor; a generator makes one
piece (optionally an evenly-spaced strip via a count param). We will iterate on
the `bmesh` implementation.

## Pieces

- **pilaster** — a single flat, rectangular engaged pier that stands proud of a
  wall plane. Params: height, width, projection (depth off the wall). Reads as a
  shallow rectangular column against a wall.
- **half_column** — the same as a pilaster but with a half-round (semicircular in
  plan) engaged profile — a more refined look. Params: height, radius, projection.
- **buttress** — a wall buttress that projects further and may taper/batter toward
  the top. Params: height, base width, projection, batter (taper).

## Files
- `assets/arch/proc/pilaster.py`

## Acceptance Criteria
- Runnable through the Blender MCP bridge, producing a viewable mesh per piece
- pilaster reads as a rectangular engaged pier; half_column as a half-round one;
  buttress as a deeper, optionally tapered projection
- Params scale the piece sensibly; a back face sits flush for placing against a wall
- Output geometry is manifold with sane normals
