---
id: rpg-d3ue
status: open
deps: [rpg-ifb2, rpg-h66f]
links: [rpg-h66f, rpg-zryp, rpg-hezs, rpg-9y61, rpg-8ot1]
created: 2026-03-04T02:56:18Z
type: task
priority: 2
assignee: KMD
---
# Phase 4 visual test: materials and shader permutations

End-to-end graphical test for Phase 4 material system and shader permutations. Renders objects with different material configurations to verify texture binding, parameter interpolation, and preprocessor-driven shader variants.

Test verifies:
- render_material_t binds albedo, normal, roughness, metallic, emissive textures to correct slots
- Shader permutations: HAS_NORMAL_MAP, HAS_VERTEX_COLOR, HAS_UV1 variants compile and render correctly
- Material registry handles survive create/lookup/destroy cycles
- Per-material uniform parameters (color tint, roughness value, metallic value) affect output
- Multiple materials in a single draw list sort correctly by material ID
- Submesh material slots: single mesh with 3 submeshes, each bound to a different material
- Fallback material: objects with no assigned material render with a default checkerboard
- Texture-less materials: flat color without textures renders correctly

Scene layout: 4x4 grid of spheres, each with a different material combination — varying albedo colors, some with normal maps, some with vertex colors, some textured, some flat. One multi-material box mesh in the center. Output: tests/output/phase4_materials.mp4

File: tests/visual/p004_visual_materials.c
Duration: 3 seconds at 30fps (90 frames)
Exit: PASS if all material permutations render without shader compile errors, frame count >= 90, no GL errors.

