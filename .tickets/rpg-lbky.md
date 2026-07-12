---
id: rpg-lbky
status: in_progress
deps: [rpg-9hw5, rpg-ljxt, rpg-gizl]
links: []
created: 2026-07-10T06:31:51Z
type: task
priority: 1
assignee: KMD
parent: rpg-lb1q
tags: [arch, materials, blender, nodes, shader]
---
# Blender node graph: sample seeds + params into base maps

Build the **Blender procedural material node graph** that constructs the base
PBR maps for each Romanesque material by **randomly sampling and blending the
flat seed textures** (from assetsrc/, sibling ticket) under control of the
**PBR parameter set** (from the research ticket), and apply the materials to the
column / arch / dome assets.

## Behaviour

- Per material, a node group that:
  - loads the material's seed images from assetsrc/materials/<material>/
  - randomly samples / mixes 2-3 seeds (per-object or per-UV-region variation,
    e.g. hash of object info or a random seed input) so instances differ
  - drives Principled BSDF: base colour = param tint x sampled albedo detail;
    roughness = param roughness +/- sampled specular detail; specular/IOR or
    metallic from params; a normal/bump derived from the detail (height -> normal)
  - respects the existing UVs (UV_SCALE = 1.0 UV/metre) — tiling scale is a param
- A parameter interface (node group inputs or a small Python API in the
  materials module under assets/arch/proc/) exposing the PBR params so a material
  can be tuned/swept without editing nodes.
- Assignment helper that applies the right material to column.py / arch.py /
  vault.py output and verifies it reads correctly on the UV layout (checker ->
  real material).

## Acceptance

Column, arched doorway, and dome each render with a plausible Romanesque
material (e.g. limestone shaft, plaster/fresco vault intrados, marble column),
detail coming from the sampled seeds, tint/roughness from params, no visible UV
seams or density mismatch. Randomisation produces visibly varied instances.

Depends on both the research ticket (params) and the seed-generation ticket
(images).


## Notes

**2026-07-10T09:00:55Z**

Step 1 (field layer) built: assets/arch/proc/material_nodes.py build_field_material samples a random box of a material's paired albedo+rough field onto UVs (per-object random offset), param-driven base roughness + z-score-normalized detail. Verified in EEVEE on limestone/marble/flint domes. Remaining components split into child subtasks.
