---
id: rpg-ilmc
status: in_progress
deps: []
links: [rpg-droh]
created: 2026-07-10T09:00:55Z
type: task
priority: 1
assignee: KMD
parent: rpg-lbky
tags: [arch, materials, blender, geometry, baking]
---
# Mesoscale pattern mask + normal baking from high-poly geometry

Bricks, stonework coursing, flint arrangement, and other MANMADE mesoscale
geometric patterns are NOT done in the shader or in the AI seeds (seeds are pure
materials; see the material-seeds-pure principle). Instead:

1. Generate real HIGH-POLY geometry for the pattern procedurally in a separate
   node graph / bpy script (brick courses/bonds, ashlar blocks with chamfered
   arrises + recessed mortar joints, knapped-flint cobble layout, etc.),
   parameterised (unit size, bond, joint width/depth, irregularity).
2. BAKE from high-poly to the low-poly target: a NORMAL map (the joint/relief),
   plus region/ID MASKS (per-unit id for per-brick material variation, and a
   mortar-vs-unit mask for the joints).
3. These maps feed the material node graph: the masks drive layer masking
   (rpg-lbky masking task) — unit material vs mortar material, per-unit tint
   variation — and the normal map drives the surface relief.

Deliverables: the high-poly pattern generator(s), the high->low bake pipeline,
and the output map convention consumed by the material graph.

