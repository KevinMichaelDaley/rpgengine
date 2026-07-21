---
id: rpg-thsd
status: open
deps: [rpg-qdr5]
links: []
created: 2026-07-21T09:32:51Z
type: task
priority: 3
assignee: KMD
parent: rpg-2lyk
---
# LA gen F4: Administration banner / screen


MODELING QUALITY BAR (see ref/archgen_dystopian_la.md section 0): NO simplified blockouts or bare primitives -- production topology from the first commit. All-quad meshes, no mesh errors, no T-junctions, good edge flow (complete loops around openings, holding edges for bevels, poles rerouted off visible flats). When in doubt, draw an ASCII topology diagram FIRST (module docstring + this ticket) referencing standard modeling practice. Programmatic validation (quad %, manifold, doubles, junction audit) in the smoke check.

ferrum.la_ab_banner per doc F4: building-scale dynamic cloth drop or rigid flickering screen, facade/spanning/pole mounts.

## Acceptance Criteria

Acceptance: (1) build_* passes the programmatic topology validation; (2) redo-panel operator with every kwarg as a property, seeded determinism; (3) DISPLAY TO USER: have the user sign off on the wireframes after viewing them interactively in a live Blender session.
