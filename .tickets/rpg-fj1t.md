---
id: rpg-fj1t
status: open
deps: []
links: []
created: 2026-07-21T01:38:36Z
type: bug
priority: 1
assignee: KMD
parent: rpg-k23d
tags: [renderer, gi, bug]
---
# Bug: gi_sdf_stream_boxes unbounded write overflows >64 SDF chunks

Section 8 #1. gi_sdf_stream_boxes (gi_sdf_stream.c:197-209) has no capacity parameter and writes n_chunks entries; caller demo_client.c:1440-1441 passes static float sbmin[64*3] -- buffer overflow the moment a level bakes >64 SDF chunks. (gi_sdf_stream_resident_boxes has a cap; this one doesn't.) Add a capacity parameter and clamp.

## Acceptance Criteria

gi_sdf_stream_boxes takes a capacity and never writes past it; a level with >64 SDF chunks does not corrupt memory.

