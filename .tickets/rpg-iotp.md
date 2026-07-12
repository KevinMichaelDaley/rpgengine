---
id: rpg-iotp
status: open
deps: []
links: [rpg-hf0x]
created: 2026-07-09T03:24:53Z
type: task
priority: 1
assignee: KMD
parent: rpg-pm1c
tags: [arch, procgen, blender, bpy, mesh]
---
# Arch mesh: arrow slit and wall niche (bpy/bmesh)

Parametric-mesh port of `rpg-hf0x` (SDF rewrite rules: wall openings) to standalone
`bpy`/`bmesh` generators under `assets/arch/proc/`. Small wall openings and
recesses. Visible in watchtower (arrow slits), library (clerestory windows, shelf
recesses), armory (rack recesses), council chamber (upper windows).

Each generator is standalone — a few parameters in, one piece out. This ticket
describes **what the piece should look like and its parameters**, not how to build
it. We will iterate on the `bmesh` implementation.

## Pieces

- **arrow_slit** — a wall-thickness segment pierced by a narrow, tall opening,
  optionally splayed (wider on the inside face). Params: clear width, height, wall
  thickness, splay amount. Reads as a defensive slit window.
- **niche** — a rectangular alcove recessed into a wall, not passing through.
  Params: width, height, recess depth, wall thickness/backing, and top style
  (flat or arched). Reads as a shelf recess / statue niche.

## Files
- `assets/arch/proc/opening.py`

## Acceptance Criteria
- Runnable through the Blender MCP bridge, producing a viewable mesh per piece
- arrow_slit reads as a narrow tall opening through the wall; splay widens the
  inner face
- niche reads as a depth-limited alcove that does NOT punch through the wall
- top style toggles niche head between flat and arched
- Output geometry is manifold with sane normals

