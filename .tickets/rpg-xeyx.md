---
id: rpg-xeyx
status: closed
deps: [rpg-x74u]
links: []
created: 2026-07-06T06:10:49Z
type: task
priority: 1
assignee: KMD
parent: rpg-vy6w
tags: [srd, voxel, critic]
---
# srd-grid-05: grid-based critic (volume, reachability, bounds)

Rewrite the critic to evaluate the SDF grid instead of box parameters. Loss terms: (1) MinimumVolume — count negative voxels per room, penalize rooms below threshold. (2) Reachability — flood-fill from entrance room, penalize unreachable rooms. (3) BoundsViolation — check SDF values at grid boundaries, penalize rooms touching edges. (4) TypeSeparation — use room_map adjacency + types. Penetration term is eliminated (rooms are regions in a shared grid, no overlap by construction). No libtorch dependency — pure C.

## Acceptance Criteria

Tests: verify each loss term responds correctly to grid modifications. Volume term increases when room shrinks. Reachability term fires when a doorway is filled in. Bounds term fires when a room touches the grid edge.

