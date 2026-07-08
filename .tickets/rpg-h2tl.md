---
id: rpg-h2tl
status: closed
deps: [rpg-ixuv]
links: []
created: 2026-07-05T22:52:21Z
type: task
priority: 1
assignee: KMD
parent: rpg-02fm
tags: [srd, foundation]
---
# srd-layout-02: srd_sdf_layout.c — init, from_grid, box ops, adjacency

Implement srd_sdf_layout_init, srd_sdf_layout_from_grid (converts existing srd_grid_t flood-fill result to SDF layout), srd_layout_add_box, srd_layout_remove_box (shifts and remaps adjacency), srd_layout_set_adj, srd_layout_get_adj, srd_layout_copy.

## Design

srd_sdf_layout_from_grid: iterate grid->regions[i], compute bounding box of all cells for that region, set hw=(xmax-xmin+1)*0.5, hd=(zmax-zmin+1)*0.5, cx=(xmin+xmax)*0.5, cz=(zmin+zmax)*0.5. Map type_char to srd_room_type_t. Set adjacency from grid->edges. srd_layout_remove_box: O(N^2) adjacency remap is fine for N<=512. 4-function rule: split into srd_sdf_layout_init.c and srd_sdf_layout_ops.c if needed.

## Acceptance Criteria

from_grid: one box per region with correct centroid and extents; adjacency mirrors grid->edges exactly; add/remove maintain adjacency consistency (no dangling entries after remove); copy produces independent deep copy; all tests in tests/srd_sdf_layout_tests.c pass

