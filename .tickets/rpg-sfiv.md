---
id: rpg-sfiv
status: open
deps: []
links: []
created: 2026-07-21T01:38:36Z
type: bug
priority: 3
assignee: KMD
parent: rpg-k23d
tags: [renderer, shadows, cleanup]
---
# Bug/cleanup: delete dead EVSM shadow code + fix false shadow docs

Section 8 #5 / 2.6. shadow_csm_blur_moments (shadow_csm_blur.c:54-90) is never called and reads GL_RG moments from an atlas that is now R32F linear depth -- leftover EVSM; the header (shadow_csm.h:198-203) falsely claims it is called by shadow_csm_bake_static. glGenerateMipmap is loaded (shadow_csm_init.c:98) but never called; u_mode (:34) is dead. Stale comments: pbr_shader.c:210 says "20 directions" (code uses 8); pbr_shader.c:256-261 documents VARIANCE/RG32F moments. Delete the dead code and fix the docs before someone re-enables a readback blur on the render thread.

## Acceptance Criteria

Dead EVSM/blur code and unused uniforms are removed; header and shader comments match the R32F/8-tap reality.

