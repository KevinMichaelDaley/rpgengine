---
id: rpg-caur
status: open
deps: [rpg-qdr5]
links: []
created: 2026-07-21T22:42:08Z
type: feature
priority: 2
assignee: KMD
parent: rpg-2lyk
---
# LA gen A1.4: stair flight + rear walkway railings

MODELING QUALITY BAR (ref/archgen_dystopian_la.md section 0): NO simplified blockouts or bare primitives. All-quad meshes, no mesh errors, no T-junctions, good edge flow; ASCII topology diagram FIRST when in doubt; programmatic validation in the smoke check. Universal rules 1-5 apply (facade+interior modes where relevant, three parameter tiers, UV unwrap as-you-go, natural vertex groups per subpart). The scissor stair flights, half-landing plates, and rear walkways are currently rail-less (loggia deck + catwalk already have solid stucco rails). Add light steel railings: round or flat-bar posts at ~1.2 m spacing, a sloped top rail parallel to the nosing line on flights (matching the 0.22 m channel stringers), level rails on landings and walkway edges, returns at the walkway ends. All prisms/sheared boxes per the stringer discipline -- planar quads, footed ends, no teeth past platforms, winding tracks travel direction (see flight()/_stringer lessons in residential.py). Gate rails off open edges only (no rail against the building wall). Optional monotony breaker: railing_style ENUM (pipe / flat-bar / missing-sections -- the last as a decay option).

## Acceptance Criteria

Acceptance: (1) passes programmatic topology validation; (2) every parameter in the redo panel, seeded determinism; (3) DISPLAY TO USER: have the user sign off on the wireframes after viewing them interactively in a live Blender session. Additionally: zero near-coincident vert pairs (<5 mm) introduced; rails join posts without T-junctions.

