---
id: rpg-27ot
status: open
deps: [rpg-7aob]
links: []
created: 2026-07-09T01:21:00Z
type: task
priority: 1
assignee: KMD
parent: rpg-9fkk
tags: [srd, ml, critic]
---
# Multi-scale per-voxel critic network

Per-voxel critic MLP with 5-scale SDF pyramid input. Predicts each voxel's contribution to the global render reward. Sum of all per-voxel critic values approximates the total reward. Trained offline; used at optimization time for dense per-voxel loss gradients without rendering.

## Design

Input pyramid (tuned for 0.125m voxels and large rooms):
  Scale 0: 8^3 @ stride 1   -> 1m   (wall surface, fine detail)
  Scale 1: 8^3 @ stride 4   -> 4m   (doorway, pillar, alcove)
  Scale 2: 8^3 @ stride 16  -> 16m  (room shape, ceiling height)
  Scale 3: 8^3 @ stride 64  -> 64m  (multi-room layout, floor plan)
  Scale 4: 8^3 @ stride 256 -> 256m (entire dungeon, inter-floor)
  + room_type one-hot (8) = 2568 floats total.
  Network: per-scale Linear(512->128)->ReLU (5 independent), concat [648], Linear(648->256)->ReLU->Linear(256->64)->ReLU->Linear(64->1). ~500K params.
  OOB samples return +1.0 (solid). Export as TorchScript .pt.

## Acceptance Criteria

Trained critic predicts per-voxel values that sum to within 10% of actual render reward on held-out grids. Runs under 0.1ms per voxel on CPU.

