---
id: rpg-zryp
status: open
deps: [rpg-lvgz, rpg-8ot1]
links: [rpg-d3ue, rpg-h66f, rpg-hezs, rpg-9y61, rpg-8ot1]
created: 2026-03-04T02:56:50Z
type: task
priority: 2
assignee: KMD
---
# Phase 6 visual test: tiled lighting, shadows, and SH probes

End-to-end graphical test for Phase 6 lighting and shadow system. Renders a scene with multiple light types, shadow-casting geometry, and ambient SH probes to verify the full tiled forward lighting pipeline.

Test verifies:
- Directional light: sun-like light casts correct PSSM shadows (4 cascades)
- Point lights: 8+ point lights with correct attenuation and tiled culling (16x16 tiles)
- Spot lights: cone-shaped illumination with correct falloff and shadow maps
- PSSM cascade splits: objects at varying distances receive correct cascade
- Shadow map quality: no shadow acne, correct bias, PCF filtering
- Light culling: only lights affecting a tile contribute (verify via debug overlay showing tile light counts)
- SH probe ambient: objects in shadowed areas receive indirect bounce lighting
- Static vs dynamic shadows: immobile lights use precomputed shadow maps
- No light bleeding through solid geometry
- Performance: maintains 60fps with 16+ active lights

Scene layout: Indoor-outdoor scene — an open room with walls (shadow casters), a directional light (sun) through a window creating PSSM shadows, 4 colored point lights inside the room, 2 spot lights as flashlights. Objects: boxes, spheres, and a ground plane at various positions. Camera slowly orbits to show shadow cascade transitions. Debug overlay toggles on at t=3s showing tile light counts as a heatmap. Output: tests/output/phase6_lighting.mp4

File: tests/visual/p004_visual_lighting.c
Duration: 5 seconds at 30fps (150 frames) — shows orbit + debug toggle
Exit: PASS if all light types render, shadows appear correct, frame count >= 150, no GL errors.

