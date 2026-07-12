---
id: rpg-pm1c
status: open
deps: []
links: [rpg-9fkk]
created: 2026-07-09T03:15:27Z
type: epic
priority: 1
assignee: KMD
tags: [arch, procgen, blender, bpy, mesh]
---
# Parametric architectural mesh primitives (Blender bpy/bmesh)

A library of small, standalone **parametric mesh generators** for architectural
pieces — arched doorways, columns, vaults, stairs, cornices, and so on — authored
in Python with `bpy` + `bmesh` and driven through the **Blender MCP bridge**.

This replaces the SDF voxel-rewrite approach to procedural architecture (epic
`rpg-9fkk` and its `SDF rewrite rules: *` children).

## What this is

Each generator is a self-contained function that takes a **handful of geometric
parameters** (dimensions plus shape controls — e.g. clearance width, height, arch
curvature, wall thickness) and returns **one architectural mesh**. You then save
it out as an asset or place/instance it into a scene using the editor.

There is **no room context, no voxel grid, and no boolean-into-a-room step**. A
generator does not know about "the room" or which wall it's on — it just builds
the piece. Assembling pieces into a level is a separate, later concern handled by
the editor (placement, snapping, instancing).

## Why the change

The SDF/voxel critic pipeline has been hard to make produce clean, controllable
architecture. Direct parametric mesh generation gives us:

- Real, inspectable polygon geometry (no marching-cubes / SVO conversion)
- A small, obvious parameter set per piece — easy to tune and to sweep
- Reusable, riggable assets that drop straight into the editor
- A fast iteration loop: the agent sends a script over the Blender MCP bridge,
  Blender builds the mesh, we screenshot / export and iterate

## Conventions

Scripts live under `assets/arch/proc/`; see `assets/arch/proc/README.md`. In short:
each module exposes one or more parametric generators, uses consistent units and a
predictable local origin/orientation so pieces line up when placed, and can be
exported to the engine's FVMA format (`scripts/export_fvma.py`) or handed to the
editor. The exact function shapes and any shared helpers are deliberately left
open — we will iterate on them.

## Scope

Port each `SDF rewrite rules: *` feature family (children of `rpg-9fkk`) to an
equivalent standalone parametric generator, one ticket per source ticket. The
feature families and their reference-dataset motivations carry over; the pieces
are now standalone parametric assets rather than grid rewrites. Each ticket
describes **what the resulting piece should look like and which parameters control
it** — not how to build it in `bmesh`.

## Deliverables

- `assets/arch/proc/` scaffold (`README.md` + conventions)
- 11 parametric generator modules (see child tickets)
- Each runnable through the Blender MCP bridge, producing a clean, manifold,
  well-originned mesh that behaves sensibly across its parameter ranges
