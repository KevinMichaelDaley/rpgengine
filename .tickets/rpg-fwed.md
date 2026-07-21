---
id: rpg-fwed
status: open
deps: [rpg-qdr5, rpg-gus3]
links: []
created: 2026-07-21T22:42:08Z
type: feature
priority: 2
assignee: KMD
parent: rpg-2lyk
---
# LA gen A1.3: address numerals on the facade

MODELING QUALITY BAR (ref/archgen_dystopian_la.md section 0): NO simplified blockouts or bare primitives. All-quad meshes, no mesh errors, no T-junctions, good edge flow; ASCII topology diagram FIRST when in doubt; programmatic validation in the smoke check. Universal rules 1-5 apply (facade+interior modes where relevant, three parameter tiers, UV unwrap as-you-go, natural vertex groups per subpart). address_text (STRING, e.g. '4623') rendered as individual sans/deco numerals mounted proud of the stucco near the entry side of the front facade (classic spot: above the carport mouth or beside the recessed ground wall), sized ~0.35 m. Reuse the la/nameplate.py curve->mesh pipeline from A1.1 with a non-cursive font param; same audit exemption for glyph caps, same UV + vertex-group rules (group 'address'). Params: address_text, address_size, address_x.

## Acceptance Criteria

Acceptance: (1) passes programmatic topology validation; (2) every parameter in the redo panel, seeded determinism; (3) DISPLAY TO USER: have the user sign off on the wireframes after viewing them interactively in a live Blender session.

