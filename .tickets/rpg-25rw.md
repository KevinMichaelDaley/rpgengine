---
id: rpg-25rw
status: open
deps: [rpg-uk6k]
links: []
created: 2026-07-09T01:21:35Z
type: task
priority: 2
assignee: KMD
parent: rpg-9fkk
tags: [srd, dataset]
---
# Training image dataset collection

Collect a set of reference dungeon renders from arbitrary camera angles to serve as the perceptual reward target. These are screenshots of good-looking dungeons that define what the critic should optimize toward.

## Design

Two sources:
  1. Generate .asc grids (hand-authored or via gen_dungeon.py), run through SRD pipeline at 0.125m voxels, render with the CPU raymarcher from many random camera angles. Save as PNGs.
  2. Screenshots from demo_client of hand-tuned dungeons.
  Store in datasets/srd_critic_training/. Need 500-1000 images covering variety of room types, scales (small corridors to great halls), and camera angles.
  Include metadata (optional): camera pose per image for analysis, but the reward function is pose-free (nearest-neighbor in feature space).

## Acceptance Criteria

Directory with 500+ PNG images of architecturally varied dungeons from diverse angles.

