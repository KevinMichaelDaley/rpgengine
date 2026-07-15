---
id: rpg-xkdz
status: closed
deps: [rpg-4x04, rpg-p2xy, rpg-nvw0]
links: []
created: 2026-07-13T05:10:22Z
type: task
priority: 2
assignee: KMD
parent: rpg-zket
---
# Pipeline graph integration + scene->pipeline hookup + integration test

Wire depth pre-pass -> cluster cull -> forward+ into the render_pipeline_* graph, hook the scene submission interface end to end, and add an integration test / example scene (built in core, invoked by a thin harness) exercising many lights with correct culling.

## Design

Core renderer. Depends on depth pre-pass, clustering, forward+ pass. Reusable; demo_client only constructs the scene.

