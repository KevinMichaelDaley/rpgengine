---
id: rpg-nvw0
status: closed
deps: [rpg-0z5c, rpg-p2xy]
links: []
created: 2026-07-13T05:10:22Z
type: task
priority: 2
assignee: KMD
parent: rpg-zket
---
# Forward+ shading pass (bind clusters + pass lights, invoke PBR shader)

The main lit pass: for each opaque renderable, bind its material + the cluster light buffers, and shade with the PBR shader using the per-cluster light set + the baked lightmap. Produces the forward+ HDR image.

## Design

Core renderer. Depends on the PBR shader (rpg-w1qe / A6) and clustered culling. Wires the per-pass light set that the shader's punctual term (rpg-pdiv) consumes.

