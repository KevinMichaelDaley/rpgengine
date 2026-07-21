---
id: rpg-iz57
status: open
deps: [rpg-1066]
links: []
created: 2026-07-21T01:37:06Z
type: task
priority: 2
assignee: KMD
parent: rpg-k23d
tags: [renderer, perf, gi]
---
# Shrink GI SDF chunks + static volume + probe buffers to fp16

Section 4.3. fp32 in the innermost march loops:
- SDF chunks GL_RGBA32F mipmapped 3D (gi_sdf_stream.c:79,143), 8 resident slots sized to the largest chunk -> ~300 MB VRAM floor; every march step is an RGBA32F trilinear tap (half/quarter-rate on Gen9/11 + old GCN).
- Static irradiance volume GL_RGB32F up to 128^3 (gi_static_volume.c:25) -> ~33 MB padded.
- Probe depth RG32F (gi_probe_gpu.c:602-617); SH/SG/pos TBOs RGBA32F.
Fix: SDF chunks -> RGBA16F (dist range ~30 m + LDR albedo fit trivially; convert in the existing upload_rgba interleave loop) or R16F dist + RGBA8 albedo; static volume -> RGB9_E5/RGBA16F; probe depth -> RG16F (mean <=30, meanSq <=900); SH TBO -> RGBA16F. Config: sdf_format. Halves march-step + fragment-tap bandwidth and ~halves GI VRAM.

## Acceptance Criteria

GI SDF chunks, static volume, and probe SH/depth buffers use fp16-class formats (per sdf_format); GI VRAM roughly halves with no visible GI change.

