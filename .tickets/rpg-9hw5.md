---
id: rpg-9hw5
status: closed
deps: []
links: []
created: 2026-07-10T06:31:51Z
type: task
priority: 1
assignee: KMD
parent: rpg-lb1q
tags: [arch, materials, pbr, research]
---
# Romanesque material types + PBR parameters (research)

Collect the set of **Romanesque architectural materials** and their **PBR
parameters** into a reference doc, using the search tool (WebSearch). This is
the parameter source that drives the procedural material node graphs (parent
rpg-lb1q) applied to the column / arch / dome assets.

## What to produce

A reference doc (e.g. ref/romanesque_materials.md) enumerating the materials
that clothe Romanesque architecture, each with a concrete PBR parameter block
usable directly in a Blender Principled BSDF / node setup:

- base colour (albedo) range — hex or linear RGB, plus a plausible variation
  range for randomisation
- roughness range
- specular / IOR (dielectric), or metallic + roughness for metals
- normal / height strength hint (how pronounced the surface relief is)
- tiling scale hint at UV_SCALE = 1.0 UV/metre (real-world feature size)
- one-line notes on what the seed texture should look like (grain, tooling
  marks, veining, mortar joints, plaster mottling, patina, etc.)

## Material list to cover (at least)

- Dressed limestone / sandstone ashlar (the dominant wall/column/voussoir stone)
- Lime plaster / fresco ground (for vault + dome intrados)
- Marble (columns / shafts / revetment)
- Granite (where used for shafts/bases)
- Bronze and wrought iron (fittings, grilles, door furniture)
- (optional) Terracotta / fired brick, timber

## Notes

Favour physically-plausible, well-sourced numbers (cite sources in the doc).
Keep it tuned to *dry interior/exterior masonry* rather than wet or polished
extremes. Output feeds child tickets: the seed-image generation prompts and the
node-graph parameter defaults.


**2026-07-10T06:35:55Z**

Drafted ref/romanesque_materials.md: 13 materials (limestone, sandstone, tufa/travertine, granite, flint, marble, fired brick, terracotta, lime plaster/fresco, bronze, wrought iron, gilding, oak) with Blender Principled PBR blocks (base color sRGB+linear, metallic, roughness range, IOR, normal strength, tiling), randomization jitter, and per-material seed-texture notes for rpg-ljxt prompts + rpg-lbky node defaults. Metal colors anchored to physicallybased.info F0; dielectrics IOR 1.5. Awaiting review before closing.
