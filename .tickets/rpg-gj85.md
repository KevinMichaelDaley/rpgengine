---
id: rpg-gj85
status: open
deps: []
links: []
created: 2026-07-21T01:37:54Z
type: task
priority: 3
assignee: KMD
parent: rpg-k23d
tags: [renderer, perf]
---
# GL capability gating: auto-degrade shadows/GI on <4.x drivers

Section 1.7. Target is OpenGL 3.3+ core but: shadow_cube.c:21 requires GL_ARB_shader_viewport_layer_array (absent on Haswell/Broadwell Intel + some Mesa); shadow_cube.c:109-112 uses GL_TEXTURE_CUBE_MAP_ARRAY (GL 4.0); GI uses #version 430 compute/SSBO/imageStore (GL 4.3). GI degrades cleanly (section 4.7) but nothing gates shadow_res>0 on the cube-array + viewport-layer features -- on a 3.3-only driver shadow init just fails. Fix: probe extensions at init and auto-degrade -- no cube-map-array -> shadow_res=0; no GL 4.3 -> gi_enabled=0; log one clear line about the effective config.

## Acceptance Criteria

On a driver missing cube-map-array/viewport-layer, shadow_res is forced to 0; missing GL 4.3 forces gi_enabled=0; one log line states the effective config; no init failure.

