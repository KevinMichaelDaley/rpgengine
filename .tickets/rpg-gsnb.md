---
id: rpg-gsnb
status: open
deps: []
links: []
created: 2026-07-21T01:36:33Z
type: task
priority: 3
assignee: KMD
parent: rpg-k23d
tags: [renderer, perf]
---
# cluster_grid_build: bin per-light instead of O(clusters x lights)

Section 1.4. cluster_grid.c:74-113: 16x16x24 = 6144 clusters; inside the per-cluster loop each light's view_transform(camera->view, l->position, vp) is recomputed (6144x per light per frame) and powf slice depths recompute per tile. 100+ lights -> >600k transform+distance tests/frame on the render thread. Fixes: hoist per-light view-space transforms into a per-frame array; precompute per-slice zn/zf and per-tile NDC bounds; better, bin per-light (compute each light's touched tile/slice range from its view-space sphere) turning O(CxL) into O(Lxtouched). Also cluster_grid.c:130 puts ~12 KB scratch on the stack (fiber hazard) -- move into cluster_grid_t.

## Acceptance Criteria

Light binning is O(lights x touched clusters); no per-light transform recompute inside the cluster loop; the ~12 KB stack scratch is moved off the fiber stack.

