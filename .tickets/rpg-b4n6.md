---
id: rpg-b4n6
status: closed
deps: []
links: []
created: 2026-07-05T22:42:34Z
type: epic
priority: 1
assignee: KMD
tags: [srd, foundation]
---
# SRD-E1: SDF Layout Representation

Foundation data structures for the new SRD pipeline. Replaces the grammar tree with a flat array of axis-aligned box SDFs with explicit center and half-extent parameters, cleanly separated from discrete structural state. All subsequent epics depend on this.

## Design

See ref/srd_redesign_plan.md §State Representation. Key types: srd_sdf_box_t {cx,cz,hw,hd,type}, srd_sdf_layout_t {boxes[MAX], n_boxes, adj[MAX][MAX]}. The adjacency matrix tracks which boxes are logically connected (corridor or shared wall). The smooth-min union and soft rasterizer live here too.

## Acceptance Criteria

- srd_sdf_layout_t initializes from srd_grid_t (ASCII flood-fill result): one box per region, center=centroid, extents=half bounding box
- Box addition/removal preserves adjacency consistency
- smooth_min of N box SDFs computes correct sign at arbitrary query points
- Soft rasterizer produces [W x H] occupancy tensor via sigmoid(-sdf/T)
- All tests green; no VLAs; no malloc/free in hot path; clean under -Wall -Wextra -Wpedantic

