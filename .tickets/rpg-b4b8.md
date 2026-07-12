---
id: rpg-b4b8
status: open
deps: [rpg-27ot, rpg-7aob, rpg-q0c2, rpg-25rw]
links: []
created: 2026-07-09T01:21:11Z
type: task
priority: 1
assignee: KMD
parent: rpg-9fkk
tags: [srd, ml, python, training]
---
# Critic training pipeline

Offline Python training script that trains the per-voxel critic using the render reward as supervision. Generates training data by running SDF grids through the raymarcher, computing perceptual reward, and fitting the critic so per-voxel sum matches the reward.

## Design

scripts/train_srd_critic.py. Training loop:
  1. Load .asc files, run SRD pipeline to get SDF grids
  2. For each grid, apply random perturbations (varying corruption levels)
  3. Render K views from random cameras using CPU raymarcher
  4. Compute perceptual reward R via encoder + training feature distance
  5. Extract 5-scale pyramid patches for all voxels
  6. Critic forward: sum of per-voxel predictions = V
  7. Loss = (V - R)^2, backprop
  Also needs negative mining: heavily corrupted grids should get high (bad) reward.
  Outputs: models/srd_voxel_critic.pt (TorchScript), models/srd_critic_encoder.pt, models/srd_critic_features.bin

## Acceptance Criteria

Trained model generalizes to unseen .asc layouts. Training converges within reasonable time. Exported .pt loads in C++ via TorchScript.

