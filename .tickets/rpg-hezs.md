---
id: rpg-hezs
status: open
deps: [rpg-nun5, rpg-9y61]
links: [rpg-d3ue, rpg-h66f, rpg-zryp, rpg-9y61, rpg-8ot1]
created: 2026-03-04T02:55:46Z
type: task
priority: 2
assignee: KMD
---
# Phase 2 visual test: draw list sorting and pipeline passes

End-to-end graphical test for Phase 2 renderer deliverables. Renders a scene with multiple objects through the full render pass pipeline (depth pre-pass, forward pass, debug overlay), using draw_list_t for state-sorted submission.

Test verifies:
- draw_list_t sorts draw calls by shader → material → mesh to minimize state changes
- Depth pre-pass writes correct depth values (verified by reading depth buffer)
- Forward pass renders all geometry with correct depth testing
- Static batching: multiple instances of the same mesh drawn in a single batched call
- Dynamic batching: moving objects re-sort each frame without visual popping
- Pipeline resource binding: UBOs for per-frame (view/proj) and per-instance (model) data
- Pass ordering: shadow → depth → forward → debug → post executes correctly
- No overdraw artifacts when objects overlap in depth

Scene layout: 20+ objects (mix of primitives) at varying depths, camera slowly pans to show depth ordering. Different objects use different shader/material combinations to exercise sort keys. Output: tests/output/phase2_draw_list_pipeline.mp4

File: tests/visual/p004_visual_draw_list.c
Duration: 3 seconds at 30fps (90 frames)
Exit: PASS if all passes execute, frame count >= 90, no GL errors.

