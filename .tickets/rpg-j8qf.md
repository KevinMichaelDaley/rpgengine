---
id: rpg-j8qf
status: open
deps: [rpg-talr]
links: []
created: 2026-02-26T04:29:27Z
type: task
priority: 2
assignee: KMD
parent: rpg-vtza
tags: [editor, texsynth, server]
---
# Texture workspace and layer system

Implement the texture synthesis workspace with named layers at fixed resolution.

READ FIRST: ref/editor_design.md §7.1 for workspace struct (texsynth_workspace_t, layers, output buffers).

Requirements:
- texsynth_workspace_t: width, height, layer_count, up to 32 layers
- texsynth_layer_t: name, type, float buffer (width*height), parameters
- Layer types: noise-filled (perlin, simplex, voronoi, fractal), solid color, gradient
- Output buffers: albedo (RGBA float), normal (RGB float), roughness (R float)
- Colorize operation: map grayscale layer through a two-color gradient
- Normal-from-height: compute normal map from height layer with strength parameter
- Memory: workspace allocates from a dedicated arena (not frame arena)

Files to create:
- include/ferrum/editor/texsynth/workspace.h
- src/editor/texsynth/texsynth_workspace.c
- src/editor/texsynth/texsynth_layer.c
- src/editor/texsynth/texsynth_colorize.c
- src/editor/texsynth/texsynth_normal.c
- tests/editor/texsynth_workspace_tests.c

