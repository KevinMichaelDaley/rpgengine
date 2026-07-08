---
id: rpg-cly7
status: closed
deps: [rpg-h2tl]
links: []
created: 2026-07-05T22:52:21Z
type: task
priority: 1
assignee: KMD
parent: rpg-02fm
tags: [srd, foundation]
---
# srd-layout-03: srd_sdf_layout_ops.c — SDF union and soft rasteriser

Implement box SDF evaluation, smooth-min union (LogSumExp), and soft occupancy rasteriser producing a 2D float grid. Pure C — no libtorch. The caller (srd_critic.cpp) wraps output in torch::from_blob for autograd.

## Design

sdf_box(cx,cz,hw,hd,qx,qz) = max(|qx-cx|/hw - 1, |qz-cz|/hd - 1). smooth_min: smin = -T*log(sum_i exp(-sdf_i/T)). occ(qx,qz) = 1/(1+exp(smin/T)). srd_layout_rasterize(layout, W, H, T, float *out): fills WxH buffer row-major. World coords: cell (gx,gz) maps to world (gx+0.5, gz+0.5).

## Acceptance Criteria

sdf_box negative inside, positive outside, zero on boundary; smooth_min of two non-overlapping boxes equals min of SDFs to within T*log(2); rasteriser output in [0,1] everywhere; finite-difference gradient of occ w.r.t. cx matches analytic to 1e-4; no libtorch dependency in .c files

