---
id: rpg-vwyk
status: in_progress
deps: []
links: []
created: 2026-07-21T01:35:45Z
type: feature
priority: 2
assignee: KMD
parent: rpg-k23d
tags: [renderer, perf, config]
---
# Add config knobs replacing env-only + missing renderer toggles

Section 7.2. Add the remaining render_config keys (struct + defaults + parse + threading through client_scene_load.c / light_stream_init.c):
- depth_prepass (0/1/auto) -- currently env-only PBR_NOPREPASS; pipeline graph already supports RENDER_PIPELINE_NODE_FLAG_DEPTH_PREPASS.
- shadow_update_interval, shadow_distance, shadow_static_cache (section 2.1).
- dir_pcf_taps, shadow_pcf_taps (incl. 1 = hard shadows; sections 2.3/2.4).
- shadow_fp16, lightmap_bands(4|9), lm_format, lm_resident_layers.
- stream_upload_mb_per_frame, stream_ram_budget_mb, stream_vram_budget_mb, sdf_resident_slots, sdf_uploads_per_frame, sdf_format.
- gi_dyn_voxel(0/1/2), gi_march_quality, gi_frag_quality, gi_prepass_scale, gi_probe_cap, gi_adaptive_ms, dir_dynamic_interval.
- texture_quality (mip drop), draw_distance/far_plane, vsync/frame_cap, static_volume_voxel, pbr_quality (shader variant select).
Many of these only take full effect once their companion code ticket lands -- add the plumbing + safe defaults here so presets can reference them.

## Acceptance Criteria

All listed keys exist in render_config_t, are parsed by render_config_parse over the defaults, and thread through to the client render path; absent keys keep today's behavior.


## Notes

**2026-07-21T01:49:04Z**

Config layer DONE: added 27 low-end knobs to render_config_t (render_config.h), behavior-preserving defaults (render_config_defaults.c), and overlay parsing (render_config_parse.c). Tests: new_knob_defaults_preserve_behavior + new_keys_overlay (13/13 pass, -Wall -Wextra clean). Every default reproduces today's behavior so configless levels are unchanged. REMAINING: per-subsystem CONSUMPTION (threading each field into client_scene_load / light_stream_init / gi_runtime / shadow paths) is owned by the individual code tickets that implement each feature -- this ticket delivered the plumbing they build on. Keep open until the shared threading (render_world_config passthrough) is wired.
