# Parametric architectural mesh primitives (`assets/arch/proc/`)

A library of standalone **parametric mesh generators** for architectural pieces —
arched doorways, columns, vaults, stairs, cornices, and so on — authored in Python
with `bpy` + `bmesh` and driven through the **Blender MCP bridge**.

This is the parametric-mesh replacement for the SDF voxel-rewrite approach to
procedural architecture (see epic `rpg-9fkk`, the `SDF rewrite rules: *` tickets).

Tracking epic: **`rpg-pm1c`** — *Parametric architectural mesh primitives
(Blender bpy/bmesh)*.

## The idea

Each generator takes a **handful of geometric parameters** (dimensions + shape
controls) and returns **one architectural piece** as a mesh. For example, an
arched-doorway generator might take a clearance width, a height, an arch curvature,
and a wall thickness, and produce a doorway mesh you can save out or place into a
scene with the editor.

Generators are **standalone**. There is no room, no wall/face selector, no voxel
grid, and no boolean-into-a-room step. A generator builds a piece; assembling
pieces into a level (placement, snapping, instancing) is a separate concern
handled later by the editor.

## How it runs

The agent sends a script to Blender over the MCP bridge; Blender builds the mesh
in-process with `bpy`/`bmesh`. The result is screenshotted and/or exported —
either as an engine asset via `scripts/export_fvma.py` (FVMA) or handed to the
editor for placement.

## Conventions

Keep pieces composable by being predictable:

- **Units** — one consistent world-unit scale across all generators.
- **Up axis** — Y up (engine convention); convert to/from Blender Z-up on export.
- **Origin & orientation** — each piece has a documented, predictable local origin
  and facing so instances line up when snapped together (e.g. a doorway centered on
  its opening at floor level; a column on its base center).
- **Parameters** — a small, named set per piece, each with a sensible range.
  Prefer a few meaningful knobs over many.
- **Clean output** — manifold, watertight where it should be, sane normals.

The exact function signatures and any shared helper module are deliberately left
open — we will iterate on them as the library grows.

## Feature modules (ticket → module)

| Ticket   | Module            | Pieces                                            |
|----------|-------------------|---------------------------------------------------|
| rpg-imlo | `vault.py`, `arch.py` | arched doorway, barrel/groin vault, dome      |
| rpg-kbuf | `pilaster.py`     | pilaster, half-column, wall buttress              |
| rpg-xr08 | `cornice.py`      | wall ledge / string course, ceiling cornice       |
| rpg-k04n | `partition.py`    | partition wall panel, arcade / colonnade run       |
| rpg-p6bd | `gallery.py`      | balcony / cantilevered gallery walkway            |
| rpg-t9l2 | `column.py`       | column (base + shaft + capital)                   |
| rpg-sae6 | `stairs.py`       | straight staircase, ramp                          |
| rpg-hf0x | `opening.py`      | arrow slit / window, wall niche / recess          |
| rpg-z40l | `basin.py`        | circular pit / basin, channel / trough            |
| rpg-rt38 | `beam.py`         | ceiling beam, coffer panel                        |
| rpg-k80x | `curved_wall.py`  | curved wall segment, apse (semicircular bay)      |

Each port ticket carries the `arch,procgen,blender,bpy,mesh` tags under epic
`rpg-pm1c` and mirrors the corresponding `SDF rewrite rules: *` source ticket.
