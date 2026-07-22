---
id: rpg-kwnr
status: closed
deps: [rpg-ezcn]
links: []
created: 2026-07-22T03:53:08Z
type: feature
priority: 1
assignee: KMD
parent: rpg-2lyk
---
# LA gen B1.1: funky storefront dress -- fabric awnings, high false fronts, mitred corner door

Per user reference photos (old urban LA strips): (a) per-tenant FABRIC AWNINGS, barrel (quarter-round arc strip, open ends per the dingbat awning precedent) or flat sloped slab, seeded type + depth 0.8-1.4 m, awning_fraction param; (b) HIGH FALSE FRONTS: per-tenant parapet faces rising 0.8-1.6 m above z_par as flat-topped raised fronts (reuse the welded _peak_wall with a wide apex flat -- gable vs flat form seeded per winning bay), merging when adjacent; (c) MITRED CORNER DOORWAY (corner_door param, bar/L layouts): ground-storey chamfer across the west-front corner, planar 45-degree _Wall carrying a glazed door, front/side ground cells void, ABUTTING-ISLAND triangular soffit at storefront-head level quadified by centroid subdivision (3 quads), upper walls continue over the notch. Quality bar + universal rules apply.

## Acceptance Criteria

Acceptance: (1) passes programmatic topology validation; (2) every parameter in the redo panel, seeded determinism; (3) DISPLAY TO USER: sign off on the wireframes interactively in a live Blender session.

