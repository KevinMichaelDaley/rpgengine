---
id: rpg-u4ir
status: open
deps: [rpg-droh]
links: []
created: 2026-07-10T09:00:55Z
type: task
priority: 1
assignee: KMD
parent: rpg-lbky
tags: [arch, materials, blender, nodes, shader]
---
# Procedural edge wear

Generate edge wear on the layered material (rpg-lbky): exposed edges and raised
areas wear to reveal an underlying/worn material (e.g. lighter exposed stone,
chipped arris), driven by geometry.

Signal source: curvature / ambient occlusion / pointiness. Cycles has Geometry
Pointiness + AO nodes; EEVEE does not expose these in-shader, so support a BAKED
curvature/AO map (bake once per object) as the EEVEE-compatible path (ties in
with the mesoscale bake task's bake pipeline).

Requirements: wear amount + edge width parameters; wear blends a wear layer over
the base via the wear mask; composites on top of the layer stack. Reproducible
bpy in assets/arch/proc/material_nodes.py.

