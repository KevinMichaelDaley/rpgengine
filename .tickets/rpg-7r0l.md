---
id: rpg-7r0l
status: open
deps: [rpg-b4b8]
links: []
created: 2026-07-09T01:21:24Z
type: task
priority: 1
assignee: KMD
parent: rpg-9fkk
tags: [srd, ml, cpp, integration]
---
# C++ critic inference integration

Load the trained TorchScript critic in C++ and integrate with the descent loop, replacing srd_grid_critic_evaluate entirely. Provides full-grid evaluation and fast delta evaluation (only re-score changed voxels).

## Design

Files: include/ferrum/procgen/srd/srd_voxel_critic.h, src/procgen/srd/srd_voxel_critic.cpp.
  C API:
    srd_voxel_critic_t *srd_voxel_critic_create(const char *pt_path);
    float srd_voxel_critic_evaluate(critic, grid, map);
    float srd_voxel_critic_evaluate_delta(critic, grid, map, changed_indices, n_changed);
    void srd_voxel_critic_destroy(critic);
  evaluate(): extract pyramid patches for all voxels, batch through MLP, sum.
  evaluate_delta(): subtract old values for changed voxels, add new values. O(n_changed) not O(n_voxels).
  In srd_descent_config_t: replace critic_cfg with srd_voxel_critic_t *critic pointer. Remove heuristic critic weights and config.

## Acceptance Criteria

Descent loop uses learned critic. Delta evaluation matches full evaluation within floating-point tolerance. No heuristic loss terms remain.

