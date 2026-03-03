---
id: rpg-m24u
status: open
deps: [rpg-07mf]
links: []
created: 2026-03-02T18:38:05Z
type: task
priority: 1
assignee: KMD
---
# Phase 2a: draw_list_t and sort-by-state rendering

Create the draw list and sort key system for minimizing GPU state changes. See ref/renderer_spec.md §3.2-3.3.

Deliverables:
- include/ferrum/renderer/draw/draw_list.h: draw_command_t (sort_key, mesh_handle, submesh_index, instance_offset/count), draw_sort_key_t (packed 64-bit key), draw_list_t (flat array + count/capacity)
- include/ferrum/renderer/draw/draw_sort.h: draw_sort_key_build() packing shader/material/mesh/depth into 64-bit key
- src/renderer/draw/draw_list.c: draw_list_init(), draw_list_push(), draw_list_clear(), draw_list_sort() (radix sort on 64-bit keys)
- src/renderer/draw/draw_sort.c: Key construction with front-to-back for opaque, back-to-front for transparent
- Sort order: shader program > material > mesh > depth
- Tests in tests/p004_renderer_draw_list_tests.c

Depends on: rpg-07mf (mesh_registry for mesh handles)

