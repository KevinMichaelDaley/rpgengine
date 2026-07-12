---
id: rpg-okya
status: open
deps: []
links: [rpg-p6bd]
created: 2026-07-09T03:24:53Z
type: task
priority: 1
assignee: KMD
parent: rpg-pm1c
tags: [arch, procgen, blender, bpy, mesh]
---
# Arch mesh: balcony / gallery walkway (bpy/bmesh)

Parametric-mesh port of `rpg-p6bd` (SDF rewrite rules: balcony, gallery, and
mezzanine) to standalone `bpy`/`bmesh` generators under `assets/arch/proc/`.
Raised walkways/galleries overlooking a floor. Prominent in council chamber
(wraparound gallery), guard checkpoint (half-wall balustrade), watchtower
(mezzanine).

Each generator is standalone — a few parameters in, one piece out. This ticket
describes **what the piece should look like and its parameters**, not how to build
it. A full wraparound gallery is assembled by placing several runs in the editor.
We will iterate on the `bmesh` implementation.

## Pieces

- **balcony** — a straight cantilevered walkway slab with a parapet/balustrade
  along its outer edge. Params: length, depth (projection), slab thickness,
  parapet height, and parapet style (solid vs balustrade). The back edge sits
  flush against a wall.

## Files
- `assets/arch/proc/gallery.py`

## Acceptance Criteria
- Runnable through the Blender MCP bridge, producing a viewable mesh
- balcony reads as a walkway slab with an outer parapet; back edge flush for
  placing against a wall
- Straight runs tile end-to-end to form longer galleries
- Params scale slab and parapet sensibly
- Output geometry is manifold with sane normals

