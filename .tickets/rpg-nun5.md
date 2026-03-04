---
id: rpg-nun5
status: in_progress
deps: [rpg-m24u]
links: []
created: 2026-03-02T18:38:17Z
type: task
priority: 1
assignee: KMD
---
# Phase 2b: render pass architecture and pipeline extension

Extend the existing render_pipeline to support the full pass architecture. See ref/renderer_spec.md §3.1.

Deliverables:
- Extend render_pipeline.h/render_pipeline_t to support: shadow pass, depth pre-pass, caster pass, light cull pass, forward pass, skybox pass, debug pass, post pass, UI pass
- Each pass has begin/submit/end callbacks plus an associated draw_list_t
- render_pipeline_execute() processes passes in deterministic order
- Per-pass framebuffer binding (shadow maps get their own FBOs)
- Integration with existing render_pipeline_graph.h dependency system
- Global per-frame UBO (FrameParams: view, proj, VP, camera_pos, time, cascade data) uploaded once before all passes
- Instance data UBO: per-instance model matrices, configurable capacity (init-time allocated pool), persistent-mapped
- Tests extending tests/p004_renderer_pipeline_tests.c

Depends on: rpg-m24u (draw lists feed into passes)

