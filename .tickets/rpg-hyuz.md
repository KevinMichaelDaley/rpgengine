---
id: rpg-hyuz
status: open
deps: [rpg-ezcn, rpg-wmes, rpg-3l2i, rpg-psto]
links: []
created: 2026-07-21T09:32:51Z
type: task
priority: 2
assignee: KMD
parent: rpg-2lyk
---
# LA gen G2: Block assembler


MODELING QUALITY BAR (see ref/archgen_dystopian_la.md section 0): NO simplified blockouts or bare primitives -- production topology from the first commit. All-quad meshes, no mesh errors, no T-junctions, good edge flow (complete loops around openings, holding edges for bevels, poles rerouted off visible flats). When in doubt, draw an ASCII topology diagram FIRST (module docstring + this ticket) referencing standard modeling practice. Programmatic validation (quad %, manifold, doubles, junction audit) in the smoke check.

ferrum.la_block per doc G2: street-ringed block of parcels with use-mix weights and corner commercial; one redo scrub = a new city block.

## Acceptance Criteria

Acceptance: (1) build_* passes the programmatic topology validation; (2) redo-panel operator with every kwarg as a property, seeded determinism; (3) DISPLAY TO USER: have the user sign off on the wireframes after viewing them interactively in a live Blender session.
