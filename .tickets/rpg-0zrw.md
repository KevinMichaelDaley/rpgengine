---
id: rpg-0zrw
status: open
deps: []
links: []
created: 2026-07-21T01:36:33Z
type: task
priority: 2
assignee: KMD
parent: rpg-k23d
tags: [renderer, perf, gi]
---
# GI: voxelize dynamic objects only on dispatch frames

Section 4.2. demo_client.c:1458 calls client_scene_voxelize_dynamic every frame: clears via a full glTexSubImage3D zero upload (gi_probe_gpu_dyn.c:61-65, ~442 KB/frame PCIe), rasterizes every dynamic mesh 3x (gi_voxelize_draw.c:60-65), and glMemoryBarrier -- but the probe compute samples u_dyn_alb once per gi_update_interval (8) frames. 7/8 of the work is discarded.
Fix: voxelize only on dispatch frames and only when a dynamic transform changed; use glClearTexImage (GL 4.4, runtime-detect) instead of the upload clear. Knob gi_dyn_voxel (0 off/static-GI-only, 1 tick cadence, 2 every frame).

## Acceptance Criteria

Dynamic voxelization runs only on GI dispatch frames (or when gi_dyn_voxel=2); the zero-upload clear is replaced by glClearTexImage where available.

