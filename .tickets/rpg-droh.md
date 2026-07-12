---
id: rpg-droh
status: in_progress
deps: []
links: [rpg-ilmc, rpg-fapy]
created: 2026-07-10T09:00:55Z
type: task
priority: 1
assignee: KMD
parent: rpg-lbky
tags: [arch, materials, blender, nodes, shader]
---
# Material layer masking / compositing

Composite multiple field layers (build_field_material, rpg-lbky step 1) into one
material via masks, per KMD's architecture. Each layer samples its own material's
baked field (random box); a mask selects between layers per pixel/region.

Mask sources to support:
- geometric region: bounding box / plane / height / distance-from-feature.
- mesh or entity attribute: vertex group, vertex colour, custom mesh attribute,
  or editor entity_attrs (e.g. paint which faces are plaster vs ashlar).
- custom mask pattern: a baked or procedural mask image (incl. the mesoscale
  pattern masks from the high-poly bake task).

Requirements: ordered stack of N layers, per-layer material + params, mask mix
between adjacent layers, feeds a single Principled BSDF (base colour + roughness,
later normal). Reuse the field-layer node group; add a masking/mix layer on top.
Reproducible bpy in assets/arch/proc/material_nodes.py.

