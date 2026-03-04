---
id: rpg-m3nv
status: open
deps: [rpg-2nxx, rpg-nun5]
links: [rpg-tqrt, rpg-q6fa, rpg-eabp, rpg-2nxx, rpg-ebpv, rpg-hrrd]
created: 2026-03-03T03:00:20Z
type: task
priority: 2
assignee: KMD
---
# Editor integration: draw lists and render submission (Phase 2)

Wire the draw list and pipeline pass system into the editor/server/client workflow. See ref/renderer_spec.md Phase 2.

Deliverables:
- Server-side: after physics tick, build a draw_list_t from all active entities (iterate edit_entity_store, construct draw_commands from mesh_handle + material + transform)
- Client-side: replace manual per-body draw loop in demo_client with draw_list_sort() + draw_list_submit() path
- Per-frame UBO (FrameParams) uploaded once before passes: view, proj, VP, camera_pos, time
- Instance data UBO: persistent-mapped, configurable instance batch capacity (init-time allocated pool)
- Extend spawn message with render_queue (opaque/transparent/overlay) so client sorts correctly
- Add SCRIPT_KEY_RENDER_QUEUE (u32, key=16) to entity_attrs.h
- IL support: add 'set_field r0 16 rN' for scripts to change render queue at runtime (e.g., fade-in/fade-out effects)
- Editor command 'render_queue' to set opaque/transparent/overlay per entity
- Tests for draw list construction from entity store

Depends on: rpg-2nxx (mesh type integration), rpg-nun5 (pipeline passes)

