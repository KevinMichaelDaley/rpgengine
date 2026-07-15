---
id: rpg-dweu
status: open
deps: [rpg-zket, rpg-1gj9]
links: []
created: 2026-07-13T06:41:00Z
type: epic
priority: 2
assignee: KMD
---
# Transparency: render-order sorting + baker light transmission

Handle transparent/translucent surfaces on both sides of the pipeline: (1) RENDER -- a sorted transparent pass (back-to-front alpha blending) after the opaque forward+ (and deferred) passes; (2) BAKER -- transparent surfaces TRANSMIT light (attenuated and colour-tinted) rather than fully occluding, so coloured light bleeds through (stained-glass-style). Visibility/form-factor tests become partial transmittance instead of binary occlusion.

## Design

Core renderer + baker; nothing in demo_client. Render: material alpha + blend mode + render queue; a transparent pass that sorts objects back-to-front and alpha-blends over the opaque result (OIT is a later option). Baker: SVO visibility rays accumulate TRANSMITTANCE through transparent voxels (from material transmission colour/opacity) instead of binary hit; the direct and solve passes tint transmitted light by the materials it passes through. TDD + extreme modularity.

## Acceptance Criteria

Transparent surfaces render correctly sorted and blended after opaque; the baker transmits coloured, attenuated light through transparent surfaces (a coloured pane casts a coloured light patch in the bake). Regression tests for transmittance accumulation and sort order. Entirely in core renderer/baker. Visual test shows both. -Wpedantic clean.

