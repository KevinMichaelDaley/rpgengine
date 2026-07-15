---
id: rpg-h9aq
status: open
deps: [rpg-1vv1, rpg-5ize, rpg-ats5, rpg-0z5c]
links: []
created: 2026-07-13T05:24:16Z
type: task
priority: 2
assignee: KMD
parent: rpg-2ejn
---
# Dynamic shadow term in material shader (combine with static + lightmap ambient) + test

Integrate the dynamic (cubemap/2D PCF) shadow term into the material shader, combined with the stationary-light static+dynamic shadow term and the lightmap ambient. Test across light types.

## Design

Core renderer. Reuses the shader shadow-term hook from rpg-ats5. Depends on the PCF sampling paths.

