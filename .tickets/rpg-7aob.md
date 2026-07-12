---
id: rpg-7aob
status: open
deps: [rpg-q0c2]
links: []
created: 2026-07-09T01:20:46Z
type: task
priority: 1
assignee: KMD
parent: rpg-9fkk
tags: [srd, ml, python]
---
# Perceptual reward function

Compute scalar reward from rendered SDF images by comparing to a training set of example dungeon screenshots. Uses a CNN encoder to map images to feature vectors; reward = sum over K rendered views of min-distance to nearest training image feature. Only used during offline critic training, never at optimization time.

## Design

Encoder: small CNN (4 conv blocks, ~200K params) mapping [3,H,W] -> [D] feature vector (D=128). Reward R(grid) = sum_k min_j ||enc(render_k) - enc(train_j)||^2. Training images pre-encoded and stored. Encoder can be pre-trained (contrastive on training set vs corrupted renders) or use frozen early VGG layers. Implemented in Python (train_srd_critic.py).

## Acceptance Criteria

Given a directory of reference PNGs and a rendered SDF image, produces a scalar reward. Lower reward = more similar to training set. Differentiable w.r.t. encoder parameters.

