---
id: rpg-ats5
status: open
deps: [rpg-7v7w, rpg-jqs3, rpg-0z5c]
links: []
created: 2026-07-13T05:23:51Z
type: task
priority: 2
assignee: KMD
parent: rpg-fsvq
---
# Co-sampled static+dynamic shadow term + material-shader integration

At shade time, sample both the static and dynamic shadow maps for a stationary light and take the nearer occluder (depth compare) to produce the shadow factor; integrate this shadow term into the material shader and combine it with the lightmap ambient.

## Design

Core renderer. Depends on static + dynamic maps and the PBR shader (rpg-w1qe). Establishes the material shader's shadow-term hook reused by dynamic shadows (rpg-2ejn).

