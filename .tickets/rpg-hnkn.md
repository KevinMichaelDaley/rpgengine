---
id: rpg-hnkn
status: open
deps: [rpg-1gj9]
links: []
created: 2026-07-13T06:41:35Z
type: task
priority: 2
assignee: KMD
parent: rpg-dweu
---
# Baker: transmittance visibility through transparent voxels

The lightmap baker's SVO visibility rays accumulate TRANSMITTANCE (product of per-material transmission) through transparent voxels instead of returning a binary hit, so light partially passes through transparent surfaces.

## Design

Core baker (src/lightmap). Transparent voxels carry transmission colour/opacity (via the material table); rays multiply transmittance, stop when it drops below a threshold.

