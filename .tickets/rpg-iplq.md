---
id: rpg-iplq
status: in_progress
deps: []
links: []
created: 2026-07-21T01:35:45Z
type: feature
priority: 1
assignee: KMD
parent: rpg-k23d
tags: [renderer, perf, config]
---
# Add render_scale (internal render target + blit) to render_config

Section 7.2 (highest-value new key). Nothing renders at sub-native resolution today; render_scale is the single biggest recoverable knob for Iris/Deck-class fill rate. Render the forward/GI passes into an internal FBO at scale (e.g. 0.75-0.85x) and blit to the swapchain. Plumbing already exists: screen_w/h flow through render_forward_config_t. Presets: low 0.75 / med 1.0 / high 1.0.

## Acceptance Criteria

A render_scale float in render_config drives an internal render target; 0.75 visibly reduces fill cost with a full-res UI still crisp; 1.0 is bit-identical to today.


## Notes

**2026-07-21T01:49:04Z**

Config half DONE: render_scale field + default (1.0 = native) + parse + tests (new_keys_overlay). REMAINING (the actual feature): allocate an internal render target at render_scale, render the forward/GI passes into it, and blit to the swapchain; thread screen_w/h * render_scale through render_forward_config_t. That GL work is unstarted.
