---
id: rpg-5qjv
status: open
deps: []
links: []
created: 2026-07-19T22:24:49Z
type: task
priority: 3
assignee: KMD
parent: rpg-hjck
tags: [exporter, lightmap, bake]
---
# Deterministic + optimized lightmap UV unwrap

The lightmap uv1 unwrap (scene_demo._gen_lightmap_uv via bpy.ops.uv.lightmap_pack) must be DETERMINISTIC: the client .fvma uv1, the baker .dmesh uv1, and the baked atlas rects (in the .flm) all depend on ONE unwrap, so any nondeterminism silently desyncs the fvma from the .flm rects (garbage lightmap sampling) on re-export. Today we work around it by always re-baking after re-export; instead the unwrap should be provably deterministic (fixed seed / stable island order / same result across runs + machines). ALSO: write an OPTIMIZED unwrap script -- lightmap_pack wastes atlas area and can splay thin geo; a purpose-built unwrap (better island packing, per-mesh luxel-density budget, seam-aware) improves lightmap quality + shrinks the atlas (less VRAM/bake time).

## Design

Options: (a) verify/repro lightmap_pack determinism (pin Blender version, margin, face order) and add a regression check that re-export reproduces byte-identical uv1; (b) replace with a custom unwrap (xatlas-style or an in-engine unwrapper) with a fixed seed + deterministic island ordering + a per-mesh texel budget from lightmap_res. Must keep fvma uv1 == dmesh uv1 == the unwrap the bake packs. See ref/gi_streaming_design.md + [[level-files-are-exporter-produced]].

## Acceptance Criteria

Re-exporting great_hall twice yields byte-identical fvma uv1 (no re-bake needed to stay consistent); optimized unwrap reduces atlas area vs lightmap_pack at equal quality.

