---
id: rpg-kwh0
status: open
deps: []
links: [rpg-oenp]
created: 2026-07-22T11:59:58Z
type: bug
priority: 1
assignee: KMD
tags: [renderer, gi, bug]
---
# Dynamic-albedo colour bleed dies at steady state once SDF chunks are resident

The gh_dyn red-banner colour bleed is written correctly but vanishes from the probe field at steady state. Established empirically with DYN_VOX_DEBUG (commit c92e2423):
- Dynamic albedo volume: correct (17 voxels, rgb 224,3,3; occluder box at 17.6,3.7,0.0).
- Probes near the banner DO receive red-dominant SH transiently (with gi_dyn_gain=8: R DC 0.212, directional 0.332 at ~frame 120) -- the full chain voxels->march->injection->SH works.
- By ~frame 360 the SH converges to a steady state that is IDENTICAL regardless of gi_dyn_gain (1 vs 8), gi_bounce (0.25..0.9), GI_EMIN (0.001), GI_HERO (4), and LEGACY_GI=1 -- i.e. at steady state the dynamic albedo is never sampled at all (binary cutoff, not dilution).
- Timing correlates with baked SDF chunk residency completing. A confirming A/B (delete .sdf chunks) is not runnable: gi_runtime does not stand up without chunks.
- The historical strong bleed predates e1effa87 (multi-bounce decay): the old undecayed accumulator integrated rare early hits forever, masking this.
Next steps: instrument WHICH branch scene_albedo takes at hit points once chunks are resident (e.g. atomic counters per branch), check hit-point positions vs the dyn volume lookup (march hit p offset / lod), and check whether pass-classify ray hits land on baked-chunk surfaces with a position that trilinear-misses the 0.35m dyn sheet. Repro: DYN_VOX_DEBUG=1 ./build/demo_client 127.0.0.1 40080 15 --level datasets/gh_dyn/great_hall.scene (server on 40080).

## Acceptance Criteria

Steady-state probe SH near the banner shows the red contribution (scales with gi_dyn_gain); the banner visibly bleeds red on the dais walls in gh_dyn.

