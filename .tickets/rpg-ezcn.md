---
id: rpg-ezcn
status: in_progress
deps: [rpg-qdr5]
links: []
created: 2026-07-21T09:32:50Z
type: task
priority: 1
assignee: KMD
parent: rpg-2lyk
---
# LA gen B1: Mini-Mall generator


MODELING QUALITY BAR (see ref/archgen_dystopian_la.md section 0): NO simplified blockouts or bare primitives -- production topology from the first commit. All-quad meshes, no mesh errors, no T-junctions, good edge flow (complete loops around openings, holding edges for bevels, poles rerouted off visible flats). When in doubt, draw an ASCII topology diagram FIRST (module docstring + this ticket) referencing standard modeling practice. Programmatic validation (quad %, manifold, doubles, junction audit) in the smoke check.

ferrum.la_minimall per doc B1: tenant run + canopy + mismatched sign band, striped lot + wheel stops, curb pole sign, L-plan corner variant, roll-up shutter fraction.

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

Story options: one_fortified (plate-steel safehouse); checkpoint_bay; dead_mall. Breakers: L-plan corner, canopy vs arcade, pole sign vs roof sign, optional second storey office strip.

RULES 4+5 (addendum): (4) UV-unwrap AS YOU GO -- every object ships with a
real, non-degenerate UV layout (seams as deliberate as edge flow; consistent
texel density); never deferred. (5) NATURAL VERTEX GROUPS per subpart as you
build (e.g. steps/windows/awnings/facade_front/carport/doors/parapet) from
the shared name vocabulary -- one-click subpart selection forever after.
