---
id: rpg-39mc
status: closed
deps: []
links: []
created: 2026-07-22T11:13:53Z
type: task
priority: 2
assignee: KMD
---
# Caustics: stream resident GI SDF chunks into shadow_caustics_set_sdf

rpg-kbqd landed the light-space caustics compute with SDF tracing, but the client never registers its resident SDF chunk textures (gi_sdf_stream / gi_runtime own them), so caustics currently run the identity redistribution (== flat tint). Plumb the resident chunk set (texture ids + origins/dims/voxel sizes, up to SHADOW_CAUSTICS_MAX_SDF=16) from the GI runtime into render_forward's shadow_caustics_set_sdf and re-bake (clear caustics_baked) when residency changes, so real geometry focuses the rays. Verify visually on la_sprawl mini-mall windows.

