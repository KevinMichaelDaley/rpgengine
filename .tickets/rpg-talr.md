---
id: rpg-talr
status: open
deps: []
links: []
created: 2026-02-26T04:29:27Z
type: task
priority: 2
assignee: KMD
parent: rpg-vtza
tags: [editor, texsynth, server]
---
# Procedural noise generators (perlin, simplex, voronoi, fractal)

Implement procedural noise functions for texture synthesis.

READ FIRST: ref/editor_design.md §7.2 for noise generation.

Requirements:
- Perlin noise (2D, 3D)
- Simplex noise (2D, 3D)
- Voronoi / cellular noise (F1, F2 distances, cell ID) with jitter parameter
- Fractal Brownian motion (octaves, lacunarity, gain)
- All functions: float result for given (x,y) or (x,y,z) + params
- Seed support for reproducibility
- Must be fast enough for real-time preview (512x512 in <50ms)
- No external dependencies (implement from scratch or use public domain reference)

Files to create:
- include/ferrum/editor/texsynth/noise.h
- src/editor/texsynth/noise_perlin.c
- src/editor/texsynth/noise_simplex.c
- src/editor/texsynth/noise_voronoi.c
- src/editor/texsynth/noise_fractal.c
- tests/editor/noise_tests.c

