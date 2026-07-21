---
id: rpg-qdr5
status: open
deps: [rpg-gh2z, rpg-5ljd]
links: []
created: 2026-07-21T09:32:50Z
type: task
priority: 1
assignee: KMD
parent: rpg-2lyk
---
# LA gen A1: Dingbat Apartment generator (pattern-setter)


MODELING QUALITY BAR (see ref/archgen_dystopian_la.md section 0): NO simplified blockouts or bare primitives -- production topology from the first commit. All-quad meshes, no mesh errors, no T-junctions, good edge flow (complete loops around openings, holding edges for bevels, poles rerouted off visible flats). When in doubt, draw an ASCII topology diagram FIRST (module docstring + this ticket) referencing standard modeling practice. Programmatic validation (quad %, manifold, doubles, junction audit) in the smoke check.

ferrum.la_dingbat per ref/archgen_dystopian_la.md A1: body with carport void, external switchback stair, facade applique sets, window grid + awnings + AC units, address numerals, balcony rails. First full exerciser of the phase-0 glue: props/menu/tags/materials/colliders end-to-end. Topology plan REQUIRED before code (carport-void loops, window loops, stair landing flow).

## Acceptance Criteria

Acceptance: (1) build_* passes the programmatic topology validation; (2) redo-panel operator with every kwarg as a property, seeded determinism; (3) DISPLAY TO USER: have the user sign off on the wireframes after viewing them interactively in a live Blender session.
