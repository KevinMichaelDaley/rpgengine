---
id: rpg-vhvf
status: open
deps: [rpg-qdr5]
links: []
created: 2026-07-21T22:42:08Z
type: feature
priority: 2
assignee: KMD
parent: rpg-2lyk
---
# LA gen A1.2: facade applique sets (starburst / mansard / tiki / script)

MODELING QUALITY BAR (ref/archgen_dystopian_la.md section 0): NO simplified blockouts or bare primitives. All-quad meshes, no mesh errors, no T-junctions, good edge flow; ASCII topology diagram FIRST when in doubt; programmatic validation in the smoke check. Universal rules 1-5 apply (facade+interior modes where relevant, three parameter tiers, UV unwrap as-you-go, natural vertex groups per subpart). facade_style currently ships only 'plain'. Implement the applique sets from ref/archgen_dystopian_la.md A1: starburst (thin-bar sputnik star + stucco diamond field), mansard (shingle strip band below the parapet), tiki (A-frame entry trim + lava-rock pier veneer band), script (thin horizontal reveal lines + oblong medallion). Each set is applied geometry welded or cleanly abutted per the corner discipline -- no floating z-fighting boxes. Sets must compose with carport/grounded fronts, partial carports, and loggias (skip or clip over the recess span like awnings/AC do).

## Acceptance Criteria

Acceptance: (1) passes programmatic topology validation; (2) every parameter in the redo panel, seeded determinism; (3) DISPLAY TO USER: have the user sign off on the wireframes after viewing them interactively in a live Blender session. Additionally: all four styles build on f2 and f3 variants incl. loggia + partial carport without audit regressions.

