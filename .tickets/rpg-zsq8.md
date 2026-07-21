---
id: rpg-zsq8
status: open
deps: []
links: []
created: 2026-07-21T01:38:36Z
type: task
priority: 3
assignee: KMD
parent: rpg-k23d
tags: [renderer, perf, gi]
---
# GI misc cost cleanups: vis-prepass cadence, hero scan cap, prepass map range

Sections 4.5 + 4.6 leftovers. Vis prepass (client_scene_load.c:580-605) re-rasterizes the whole scene at w/8 x h/8 EVERY frame + CPU scan of ~32k px + client_scene_stream_probes over all probes x resident boxes, but gi_sdf_stream_page consumes it only every update_interval frames -- run the prepass at paging cadence (the PBO ping-pong tolerates staleness); glMapBuffer whole-buffer (gi_vis_prepass.c:113,200) -> glMapBufferRange(GL_MAP_READ_BIT); early-out the probe rescan (:560-571) on an unchanged resident-box hash. Hero top-K scan (gi_probe_gpu.c:383-390) is O(sources<=1024)/probe with hybrid on -- cap (~256) or reuse last tick's heroes with periodic rescan.

## Acceptance Criteria

The vis prepass runs at paging cadence with a ranged read-map and hashed early-out; the hero scan is capped/reused; steady-state GI is unchanged.

