---
id: rpg-2vfm
status: closed
deps: []
links: []
created: 2026-07-20T06:01:45Z
type: task
priority: 2
assignee: KMD
---
# Expose probe-visibility + specular knobs in render_config


`gi_vis_bias` / `gi_vis_varmin` / `gi_vis_sharp` -- the probe Chebyshev softening knobs that control the probe-lattice **dot / triangle artifacts** near surfaces (`gi_runtime.c:70-74`) -- and `spec_lobes` are **env-only** (`GI_VIS_BIAS`, `GI_VIS_VARMIN`, `GI_VIS_SHARP`, `GI_SG_LOBES`). The client render path therefore runs them blind at their defaults and they cannot be tuned per level; during GI tuning they had to be passed on the command line.

Plumb them through `render_config` (JSON) -> `render_world_config` -> `gi_runtime_config`, exactly as `gi_smooth` already is, preserving current defaults.

Also still env-only after the MIS/hybrid work and worth the same treatment: `GI_BOUNCE`, `GI_SAMPLES`, `GI_HERO`, `GI_HYBRID`, `GI_MIS`, `GI_STAT_SCALE`, `GI_NORM_GATE`.

## Acceptance Criteria

A level's render_config JSON can set the probe-visibility softening + specular lobe count; no GI tuning requires env vars.
