---
id: rpg-6p1o
status: open
deps: [rpg-j8qf]
links: []
created: 2026-02-26T04:29:27Z
type: task
priority: 2
assignee: KMD
parent: rpg-vtza
tags: [editor, texsynth, server]
---
# UV bake engine (rasterize texture to UV map)

Implement the UV bake engine that rasterizes procedural textures onto mesh UV maps.

READ FIRST: ref/editor_design.md §7.3-7.4 for UV bake implementation (triangle rasterization, world-to-UV mapping).

Requirements:
- Load mesh UV coordinates (from glb/obj)
- For each triangle in UV space, rasterize and sample the procedural texture at the corresponding world-space position
- Output: PNG files for albedo, normal, roughness maps
- Support UV set selection (--uv 0, --uv 1)
- Output resolution configurable (--res 512, --res 1024, etc.)
- Can be parallelized via job system (per-triangle rasterization)
- Seam handling: 1-pixel border dilation to prevent seam artifacts

Files to create:
- include/ferrum/editor/texsynth/uv_bake.h
- src/editor/texsynth/texsynth_uv_bake.c
- src/editor/texsynth/texsynth_rasterize.c
- src/editor/texsynth/texsynth_png_write.c
- tests/editor/texsynth_uv_bake_tests.c

