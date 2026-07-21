---
id: rpg-da8c
status: closed
deps: [rpg-zygg]
links: []
created: 2026-07-19T22:33:49Z
type: task
priority: 1
assignee: KMD
parent: rpg-hjck
tags: [renderer, client, config]
---
# Render-world config loadable from JSON (not hardcoded)

The client's render_world config (forward+ cluster, sh_scale/normal_bias, sun+CSM+cube/spot shadow params, GI: probe grid, static irradiance volume, sky AO, static weights, spec gain) is currently HARDCODED in client_scene_load.c and has DIVERGED from the working hall_lit_dynamic setup (dynamic point/spot lights + lightmap specular missing). Make it a JSON render-config schema (headless parser like scene_desc) loaded at runtime, with a default JSON that matches hall_lit_dynamic EXACTLY. The client (and ideally the demo) load the same config so there is ONE source of truth and the pipeline features are reused, not reimplemented. Ships as the level's render settings or an engine default JSON.

## Design

New headless render_config schema + json parser; client_scene_load reads it instead of hardcoding cfg. Default JSON mirrors hall_lit_dynamic lines ~924-1178 (fcfg + rwcfg). Do NOT fork the render loop -- reuse render_world/render_forward.

## Acceptance Criteria

client_scene_load builds render_world from a JSON config; great_hall shows the dynamic lights + lightmap specular matching hall_lit_dynamic; no hardcoded render params in C.


## Notes

**2026-07-19T23:38:26Z**

CONFIG VARS to expose (hardcoded in client_scene_load.c today, causing visible tuning issues): sh_scale (lightmap brightness -- currently 0.7, user reports the lightmap reads TOO BRIGHT; must be a config var), sh_normal_bias (0.5), ambient, sun intensity scale (CLIENT_SUN_ENERGY_SCALE 0.45 in find_sun), CSM params (dir_cascades/res/lambda/bias/softness), cube-shadow params (res/near/far/bias), GI: static_k, sky_ao_color/ref/mult, static_baked_w/static_dyn_w, spec_gain, gi_update_interval/n_probe_groups, probe grid params. Default JSON must match hall_lit_dynamic. sh_scale is the immediate one -- lightmap over-bright.
