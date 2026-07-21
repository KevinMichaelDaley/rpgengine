---
id: rpg-3exi
status: open
deps: [rpg-1066]
links: [rpg-f6dj]
created: 2026-07-21T01:37:06Z
type: task
priority: 1
assignee: KMD
parent: rpg-k23d
tags: [renderer, perf, gi]
---
# Shrink SH lightmap atlases to RGBA16F (VRAM + per-pixel fetch)

Sections 5.1 + 3.3 (biggest VRAM item AND a per-pixel win). Nine coefficient GL_RGB32F 2D arrays (light_stream_init.c:162-172, client_scene_lightmap.c:46-55) = 108 B/texel (144 padded to RGBA32F). One 1024 chunk layer = 113-150 MB; CLIENT_LM_MAX_RESIDENT=8 -> up to ~1 GB; the single-atlas path uploads ~450 MB at load for a 2048 atlas. Catastrophic on 4 GB cards and shared-memory APUs. And sh9_fetch (pbr_shader.c:146-150) does 9 texture() calls into these per pixel + sh9_radiance re-runs the basis for specular.
Fixes: repack RGBA16F (or RG11F_B10F) -> 108 -> 32 (or ~14) B/texel; lightmap_bands low mode (L1 = 4 coeffs, SH4 at sample time, 55% fewer pages, comment at :135-137 already proposes it); low tier drops lightmap specular (keep env_brdf_approx*irradiance); make lm_resident_layers a knob.

## Acceptance Criteria

SH lightmap atlases are RGBA16F (or the configured lm_format); lightmap_bands=4 fetches only u_sh0..3; resident VRAM drops ~4-6x with no visible irradiance change.

