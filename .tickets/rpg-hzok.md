---
id: rpg-hzok
status: closed
deps: [rpg-bytk, rpg-omqm]
links: []
created: 2026-07-05T06:56:51Z
type: task
priority: 0
assignee: KMD
parent: rpg-q5eq
tags: [procgen]
---
# srd-025: Milestone 6 smoke — VLM-in-the-loop: user prompt → full dungeon

DO NOT CREATE STUBS OR PARTIAL IMPLEMENTATIONS; NEVER SKIP FEATURES OR DEFER STEPS!

Integration test after architect prompt (srd-014) rewired and full pipeline (M5) verified. Wire the VLM into the pipeline end-to-end: a natural-language user prompt → VLM generates ASCII grid + LOSS expression → parser + SRD + SVO → renderable mesh. The test prompt should describe a dungeon with a loose gameplay purpose (e.g., 'build a 3-floor dungeon where guards patrol between the entrance and treasure vault, and the armory has a clear sightline to the barracks'). The VLM must produce a valid ASCII grid and loss composition that reflects the spatial intent of the prompt.

RED-phase: tests/procgen/srd/srd_m6_vlm_smoke.cpp — send user prompt to VLM via the architect module, capture ASCII+LOSS output, parse it, run SRD, build SVO, verify mesh is valid. Verify: VLM output parseable by both ASCII parser and loss compiler; loss terms reflect prompt intent (e.g., a LineOfSight term appears if the prompt mentions visibility); final dungeon geometry has rooms of the types mentioned in the prompt; the generated dungeon is structurally reasonable (rooms not all the same size, multiple floors present if requested, stairs connect floors).

## Acceptance Criteria

VLM output is a valid ASCII grid + LOSS block parseable without errors; LOSS expression contains terms that reflect the prompt's spatial requirements; final SVO has >0 solid voxels; all room types from prompt appear in output; dungeon spans the expected number of floors; mesh generation succeeds without errors; pipeline completes from prompt to mesh within time budget

