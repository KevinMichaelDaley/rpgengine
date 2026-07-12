---
id: rpg-g3qe
status: open
deps: []
links: [rpg-k80x]
created: 2026-07-09T03:25:23Z
type: task
priority: 2
assignee: KMD
parent: rpg-pm1c
tags: [arch, procgen, blender, bpy, mesh]
---
# Arch mesh: curved wall segment and apse (bpy/bmesh)

Parametric-mesh port of `rpg-k80x` (SDF rewrite rules: circular room and apse) to
standalone `bpy`/`bmesh` generators under `assets/arch/proc/`. Curved wall pieces
for round rooms and semicircular bays. Visible in watchtower (circular room),
council chamber (near-round), ritual chamber (rounded), and apse extensions.

Each generator is standalone — a few parameters in, one piece out. This ticket
describes **what the piece should look like and its parameters**, not how to build
it. A full rotunda is assembled by placing several curved segments in the editor.
We will iterate on the `bmesh` implementation.

## Pieces

- **curved_wall** — a wall segment curved in plan (an arc). Params: radius, arc
  angle (sweep), height, thickness. Segments tile end-to-end to form rotundas /
  round rooms; a 360° sweep gives a full cylindrical wall.
- **apse** — a semicircular bay: a half-round wall, optionally capped by a
  half-dome. Params: radius, height, thickness, and capped (half-dome on/off).
  Reads as a rounded alcove extending off a wall.

## Files
- `assets/arch/proc/curved_wall.py`

## Acceptance Criteria
- Runnable through the Blender MCP bridge, producing a viewable mesh per piece
- curved_wall reads as an arc-in-plan wall; arc angle sweeps from a shallow curve
  to a full circle; segments tile end-to-end
- apse reads as a semicircular bay; the cap toggle adds/removes a half-dome
- Params scale radius/height/thickness sensibly
- Output geometry is manifold with sane normals

