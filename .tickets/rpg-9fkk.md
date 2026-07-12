---
id: rpg-9fkk
status: open
deps: []
links: [rpg-pm1c]
created: 2026-07-09T01:20:24Z
type: epic
priority: 1
assignee: KMD
tags: [srd, critic, ml]
---
# SRD learned visual critic

Replace heuristic grid critic with a learned per-voxel critic trained on render-reward. The reward is a perceptual loss between SDF raymarched images and a training set of example dungeon renders. The critic learns to interpolate this global reward to every SDF cell, giving dense per-voxel gradients. The heuristic loss terms (volume, reachability, bounds, separation) are removed entirely.

