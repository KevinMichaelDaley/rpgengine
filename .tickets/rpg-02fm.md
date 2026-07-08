---
id: rpg-02fm
status: closed
deps: []
links: []
created: 2026-07-05T22:51:00Z
type: epic
priority: 1
assignee: KMD
tags: [srd, foundation]
---
# SRD-E1: SDF Layout Representation

Foundation data structures for the new SRD pipeline. Replaces the grammar tree with a flat array of axis-aligned box SDFs (srd_sdf_box_t) with explicit centre and half-extent parameters, cleanly separated from discrete structural state (which boxes exist, adjacency graph). All subsequent epics depend on this.

## Design

See ref/srd_redesign_plan.md §State Representation and §Differentiable Soft Rasteriser. srd_sdf_box_t {cx,cz,hw,hd,type,flags}. srd_sdf_layout_t {boxes[512], n_boxes, adj[512*512]}. SRD_EPSILON=0.01f enforces jump continuity. The smooth-min (LogSumExp) SDF union and sigmoid soft rasteriser live here.

## Acceptance Criteria

srd_sdf_layout_from_grid produces one box per ASCII region with correct centroid/extents; adjacency mirrors grid edges; smooth-min SDF is negative inside union, positive outside; soft rasteriser output in [0,1]; gradient of rasteriser w.r.t. cx matches finite difference to 1e-4; no VLAs; no per-frame malloc; clean -Wall -Wextra -Wpedantic

