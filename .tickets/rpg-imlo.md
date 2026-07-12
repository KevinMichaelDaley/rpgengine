---
id: rpg-imlo
status: closed
deps: []
links: [rpg-n1kq]
created: 2026-07-09T03:17:06Z
type: task
priority: 1
assignee: KMD
parent: rpg-pm1c
tags: [arch, procgen, blender, bpy, mesh]
---
# Arch mesh: vaults and arches (bpy/bmesh)

Parametric-mesh port of `rpg-n1kq` (SDF rewrite rules: vaults and arches) to
standalone `bpy`/`bmesh` generators under `assets/arch/proc/`. Vaults and arched
doorways define ~90% of the visual character of the dwarven reference dataset.

Each generator is standalone — a few parameters in, one mesh out. This ticket
describes **what the piece should look like and its parameters**, not how to build
it. We will iterate on the `bmesh` implementation.

## Pieces

- **arched_doorway** — a doorway opening with a rounded top. Params: clearance
  width, height (to spring line), arch curvature (flat lintel → full semicircle),
  wall thickness, and reveal/jamb depth. Result: a wall-thickness doorway frame
  with an arched head.
- **barrel_vault** — a half-cylinder vaulted ceiling / tunnel section. Params:
  span, length, rise (shallow → full semicircle), shell thickness.
- **groin_vault** — a single cross-vault bay: two barrel vaults meeting at right
  angles with sharp diagonal groin lines rising to a center point. Params: bay
  width, bay depth, rise, thickness.
- **dome** — a hemispherical dome cap. Params: diameter, rise (saucer → full
  hemisphere), shell thickness, optional oculus radius.

## Files
- `assets/arch/proc/arch.py` (arched_doorway)
- `assets/arch/proc/vault.py` (barrel_vault, groin_vault, dome)

## Acceptance Criteria
- Runnable through the Blender MCP bridge, producing a viewable mesh per piece
- arched_doorway reads as a doorway with a rounded head; curvature param sweeps
  from a flat lintel to a full semicircle
- barrel_vault reads as a half-cylinder tunnel; groin_vault shows crossing vaults
  with visible groin lines; dome reads as a hemisphere
- Consistent local origin/orientation so pieces can be placed and snapped
- Output geometry is manifold with sane normals
