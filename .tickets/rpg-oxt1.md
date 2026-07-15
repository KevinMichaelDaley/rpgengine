---
id: rpg-oxt1
status: open
deps: []
links: []
created: 2026-07-15T08:23:06Z
type: task
priority: 2
assignee: KMD
parent: rpg-oyuf
---
# Route demo-client textures/meshes/lightmaps through the resource paradigm


## Notes

**2026-07-15T08:23:31Z**

Apply the fiber-based resource paradigm (rpg-h553) to the demo client: create every PBR texture, static mesh, and lightmap/SH atlas through the job-system loader fibers -> gpu_cmd_queue -> render-thread gpu_executor (with GPU_CMD_CUSTOM finalisers calling texture_create/static_mesh_create), backed by gpu_registry + arena. Pattern proven in tests/visual/hall_lit_dynamic.c and tests/visual/resource_pipeline.c. Part of the renderer-into-demo-client migration (parent rpg-oyuf).
