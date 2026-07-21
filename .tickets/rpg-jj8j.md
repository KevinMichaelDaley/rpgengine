---
id: rpg-jj8j
status: open
deps: [rpg-1066]
links: [rpg-9u96]
created: 2026-07-21T01:37:06Z
type: task
priority: 2
assignee: KMD
parent: rpg-k23d
tags: [renderer, perf, shadows]
---
# Shrink shadow maps to R16F + 16/24-bit depth

Section 2.5. All shadow color targets are R32F: shadow_csm_init.c:51 (dyn map), :125 (static atlas), shadow_cube.c:110, shadow_spot.c:80. Stored values are distances normalized to [0,1]; R16F ~10-bit mantissa is sufficient at the shipped biases (shadow_bias=0.08, dir_bias=0.05). Also drop cube depth attachment DEPTH_COMPONENT32F -> 16/24-bit (shadow_cube.c:126) -- depth is only a z-test surface, linear distance lives in the R32F/R16F color. Halves write bandwidth of every shadow pass, sample bandwidth of every tap (sections 2.3/2.4), and the clear (2.1). shadow_atlas_config_t.internal_format already parameterizes the atlas. Knob shadow_fp16 (default 1).

## Acceptance Criteria

Shadow color targets are R16F and depth attachments 16/24-bit under shadow_fp16=1; shadows look unchanged at the shipped biases.

