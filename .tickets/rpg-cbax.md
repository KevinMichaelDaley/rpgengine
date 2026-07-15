---
id: rpg-cbax
status: open
deps: [rpg-kwls, rpg-oj8w, rpg-0z5c, rpg-7xl3]
links: []
created: 2026-07-13T06:41:34Z
type: task
priority: 2
assignee: KMD
parent: rpg-nt4y
---
# Integrate POM into PBR material/shader (displaced UV drives maps + shadow term)

Wire the parallax-displaced UV to drive all material maps (albedo/normal/roughness/metal/AO) and multiply direct + punctual lighting by the self-shadow term. Material carries the cone/SDF map + shallow/shadow depth scales + step budget. Edge/silhouette handling.

## Design

Core renderer. Depends on the raymarch (G2), self-shadow (G3), material (rpg-7xl3), shader wrapper (rpg-0z5c).

