---
id: rpg-0hmp
status: closed
deps: [rpg-2ijr, rpg-cly7]
links: []
created: 2026-07-05T22:54:51Z
type: task
priority: 1
assignee: KMD
parent: rpg-t9ga
tags: [srd, critic, libtorch]
---
# srd-critic-02: AnalyticalCritic implementation

Implement all AnalyticalCritic loss terms as differentiable libtorch operations. Each term must flow gradient back to layout_params. The rasteriser output (from srd_layout_rasterize wrapped in torch::from_blob) feeds the SDF-based terms. Other terms operate directly on layout_params tensor.

## Design

NonPenetration: for each pair (i,j), compute SDF overlap = max(0, hw_i+hw_j - |cx_i-cx_j|) * max(0, hd_i+hd_j - |cz_i-cz_j|); sum. MinimumSize: torch::relu(min_size - params[:,2]).pow(2) + torch::relu(min_size - params[:,3]).pow(2) summed by type mask. SoftReachability: build soft adjacency matrix A from SDF overlaps, run 10 power iterations, penalise if any node has low reachability score. BoundsViolation: relu(cx-hw - 0).pow(2) + relu(cx+hw - W).pow(2) etc.

## Acceptance Criteria

loss.backward() produces non-zero grad for all params; NonPenetration loss is monotonically increasing as overlap increases (finite diff test); MinimumSize loss is zero when all boxes exceed min_size; BoundsViolation loss is zero for fully in-bounds layout; gradient matches finite difference to 1e-3 for each term independently

