---
id: rpg-rxf8
status: open
deps: [rpg-jqui]
links: []
created: 2026-07-22T10:15:50Z
type: feature
priority: 1
assignee: KMD
---
# Renderer: sorted translucent pass in forward+

4th pipeline-graph node after 'forward': collect renderables whose material opacity < 1 (skipped by the opaque loop), sort back-to-front by view-space AABB-center depth, draw with GL_BLEND (SRC_ALPHA, ONE_MINUS_SRC_ALPHA), depth test LEQUAL with depth WRITE OFF, full clustered lighting + shadows + u_opacity alpha.

## Acceptance Criteria

Headless GL test: two overlapping tinted glass quads; framebuffer pixel equals the correct back-to-front blend within tolerance (and differs from the wrong-order blend).

