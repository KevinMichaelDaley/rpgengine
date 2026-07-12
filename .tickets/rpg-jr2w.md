---
id: rpg-jr2w
status: open
deps: []
links: [rpg-sae6]
created: 2026-07-09T03:24:53Z
type: task
priority: 1
assignee: KMD
parent: rpg-pm1c
tags: [arch, procgen, blender, bpy, mesh]
---
# Arch mesh: staircase and ramp (bpy/bmesh)

Parametric-mesh port of `rpg-sae6` (SDF rewrite rules: stairs and ramps) to
standalone `bpy`/`bmesh` generators under `assets/arch/proc/`. Stepped and ramped
level connections. Visible in throne room (steps to dais), council chamber (tiered
seating), watchtower, quarry (stepped ledges), mine depot (loading ramps).

Each generator is standalone — a few parameters in, one piece out. This ticket
describes **what the piece should look like and its parameters**, not how to build
it. We will iterate on the `bmesh` implementation.

## Pieces

- **staircase** — a straight flight of steps rising from bottom to top. Params:
  width, total rise (height climbed), step count (or step height + going/depth),
  and riser style (closed vs open). Origin at the bottom front edge.
- **ramp** — a straight inclined slab. Params: width, run length, total rise
  (which sets the slope), slab thickness. Origin at the bottom front edge.

## Files
- `assets/arch/proc/stairs.py`

## Acceptance Criteria
- Runnable through the Blender MCP bridge, producing a viewable mesh per piece
- staircase reads as an even flight of steps; step count/height/going track params
- ramp reads as a straight incline whose slope follows run length vs rise
- Bottom-front origin so pieces seat on a floor and align to a landing
- Output geometry is manifold with sane normals

