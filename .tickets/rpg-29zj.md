---
id: rpg-29zj
status: open
deps: [rpg-jqui]
links: []
created: 2026-07-22T10:15:50Z
type: feature
priority: 1
assignee: KMD
---
# Renderer: CSM translucency mask targets (transparent shadows)

Per KMD's architecture step 1: the shadow caster pass (static cascades + dynamic map) generates an ADDITIONAL depth-enabled translucency mask. Translucent casters are excluded from the main shadow map (light passes through) and rendered into the mask instead: an RGBA16F color atlas (rgb transmission tint, a coverage) + an R32F distance atlas, nearest-surface via depth test, one layer per cascade (shadow_atlas allocator, new internal formats) + a dynamic-map pair. Receiver side (pbr_shader): after the opaque CSM test passes, if the receiver lies BEYOND the mask depth (behind the glass as seen from the light), the sun contribution is multiplied by the mask tint*alpha -- gated exactly as 'can I see through the translucent surface to this pixel'.

## Acceptance Criteria

Headless GL test: ground plane + tinted glass quad + sun. Assert: glass absent from the main shadow map, present in the mask; framebuffer pixel in the glass shadow reads tinted (not black), pixel outside reads full sun.

