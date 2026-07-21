---
id: rpg-gh2z
status: in_progress
deps: []
links: []
created: 2026-07-21T09:32:50Z
type: task
priority: 1
assignee: KMD
parent: rpg-2lyk
---
# LA gen phase 0: shared operator glue (params/menu/seed/collections)


MODELING QUALITY BAR (see ref/archgen_dystopian_la.md section 0): NO simplified blockouts or bare primitives -- production topology from the first commit. All-quad meshes, no mesh errors, no T-junctions, good edge flow (complete loops around openings, holding edges for bevels, poles rerouted off visible flats). When in doubt, draw an ASCII topology diagram FIRST (module docstring + this ticket) referencing standard modeling practice. Programmatic validation (quad %, manifold, doubles, junction audit) in the smoke check.

la/params.py + la/menu.py: property-from-signature decorator (single source of truth for defaults/ranges), declarative Add > Dystopian LA menu table, seeded RNG helper, per-invocation collection management, topology validation helpers (quad %, manifold, doubles, T-junction audit) used by every generator's smoke check. Pattern consumers: every tool below.

## Acceptance Criteria

Acceptance: (1) build_* passes the programmatic topology validation; (2) redo-panel operator with every kwarg as a property, seeded determinism; (3) DISPLAY TO USER: have the user sign off on the wireframes after viewing them interactively in a live Blender session.

## GENERATOR COMPLETENESS RULES (universal, see ref/archgen_dystopian_la.md 1b)

1. TWO MODES: `facade` AND `interior` -- interior adds all structural walls,
   party walls, load-bearing columns/beams, slabs, stair/corridor cores, as
   just-built and fully walkable. NO furniture/carpet/doors (separate tasks).
   Interior shares the global line grid + the same topology bar.
2. THREE PARAMETER TIERS: numeric variation of most features; MONOTONY
   BREAKERS (optional major structural elements that change the massing --
   e.g. an optional carport lowers the building to grade when absent;
   alternate footprint shapes where feasible); and story options.
3. STORY OPTIONS: 2-3 "particularly interesting" switches, OFF by default,
   telling a coherent thematic story (drought / abandonment / regime /
   resistance). See the doc 3b table for this tool's canonical set.

Framework impact: params.py grows a shared `mode` enum (facade/interior) + a story-option property convention (off-by-default BoolProperties grouped last in the redo panel).
