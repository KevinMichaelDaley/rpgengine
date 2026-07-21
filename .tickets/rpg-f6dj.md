---
id: rpg-f6dj
status: open
deps: []
links: [rpg-3exi]
created: 2026-07-21T01:37:54Z
type: task
priority: 2
assignee: KMD
parent: rpg-k23d
tags: [renderer, perf, streaming]
---
# Per-frame upload byte budgets for lightmap + SDF streaming

Sections 5.2 + 4.6. Lightmap: client_ls_upload (light_stream_cb.c:86-94) does 9 full-chunk glTexSubImage3D synchronously; asset_stream_tick.c:91-127 promotes up to max_in_flight=4 chunks/tick -> up to 36 large uploads / hundreds of MB in one frame on a camera turn. SDF page-in (gi_sdf_stream.c:212-235): CPU voxel interleave + 14-33 MB RGBA32F glTexSubImage3D + glGenerateMipmap(GL_TEXTURE_3D) (a multi-ms Intel/GCN stall), synchronous and uncapped (:257-268 pages every newly-visible chunk).
Fix: per-frame upload BYTE budget in client_light_stream_tick (upload N coeff planes/tick, resume next frame -- slot keeps coeff[] RAM until uploaded); cap SDF to 1 chunk/frame; precompute SDF mips offline in the .sdf. Knobs stream_upload_mb_per_frame (4/16/64), sdf_uploads_per_frame.

## Acceptance Criteria

Lightmap and SDF uploads respect a per-frame byte budget and resume across frames; a camera turn no longer spikes from a burst of large glTexSubImage3D + 3D mipgen calls.

