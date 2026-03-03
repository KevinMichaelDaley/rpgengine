---
id: rpg-oyuf
status: open
deps: [rpg-j184]
links: []
created: 2026-03-02T18:40:28Z
type: task
priority: 2
assignee: KMD
---
# Demo client migration to new renderer

Incrementally migrate tests/examples/demo_client.c from ad-hoc rendering to the new renderer type system. See ref/renderer_spec.md §14.

Migration steps (can be done incrementally as each phase lands):
1. Replace gen_*_mesh() with static_mesh_create_from_primitives() (after Phase 1a)
2. Replace per-body VBO/VAO with mesh_registry lookups (after Phase 1c)
3. Replace manual draw loop with draw_list + sort + submit (after Phase 2a)
4. Wire entity hierarchy to scene_graph_t, use world transforms from scene graph (after Phase 3)
5. Replace inline shader strings with material registry, capsule shader becomes a material variant (after Phase 4)
6. Integrate skeletal_mesh_t for character rendering (after Phase 5)
7. Add lights, shadow maps, tiled culling, replace flat u_color with full surface shader (after Phase 6)

During migration, both old and new paths coexist. body_render_info_t continues to work for physics debug visualization. Old FVMA pipeline unchanged.

Depends on: rpg-j184 (can start migration as soon as static_mesh_t exists)

