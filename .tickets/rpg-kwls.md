---
id: rpg-kwls
status: open
deps: [rpg-f5m4]
links: []
created: 2026-07-13T06:41:34Z
type: task
priority: 2
assignee: KMD
parent: rpg-nt4y
---
# Tangent-space view-ray cone-step raymarch -> shallow parallax UV

Shader: march the view ray in tangent space using cone stepping (G) accelerated by the SDF (B), capped by the complexity budget (A), to find the parallax-displaced UV. Use a SHALLOW displacement scale to minimise silhouette stretch / texture swim at grazing angles.

## Design

Core renderer, in the PBR shader. Relaxed cone stepping + SDF refine. Shallow display depth is a deliberate knob distinct from shadow depth.

