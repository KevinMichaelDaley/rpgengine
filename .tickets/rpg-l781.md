---
id: rpg-l781
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
# Texture blend modes

Implement blend operations for combining texture layers.

READ FIRST: ref/editor_design.md §7 for blend operations.

Requirements:
- Blend modes: multiply, add, subtract, overlay, screen, mix (lerp)
- blend(mode, layer_a, layer_b, factor) → output layer
- Factor is 0.0-1.0 controlling blend strength
- Operate on float buffers (per-pixel, width*height)
- Must be fast (vectorizable inner loops)

Files to create:
- src/editor/texsynth/texsynth_blend.c
- tests/editor/texsynth_blend_tests.c

