---
id: rpg-0wls
status: open
deps: [rpg-tep1]
links: []
created: 2026-07-05T06:26:00Z
type: task
priority: 0
assignee: KMD
parent: rpg-q5eq
tags: [procgen]
---
# srd-015: E2E integration tests

**DO NOT CREATE STUBS OR PARTIAL IMPLEMENTATIONS; NEVER SKIP FEATURES OR DEFER STEPS!**



End-to-end test after the full pipeline (srd-013 SVO integration) is complete, before the VLM is wired in. Uses hand-written ASCII+LOSS files (no VLM dependency) to test the complete ASCII→SRD→SVO→mesh chain at scale: 3 floors, 15+ rooms, multiple loss terms. RED-phase: tests/procgen/srd/srd_e2e_tests.cpp

(Note: VLM-in-the-loop testing is deferred to M6 / srd-025.)

## Acceptance Criteria

All rooms connected; no invalid overlaps; stairs align across floors; solid voxel count is reasonable for the scene scale; no SVO initialization errors; stress test completes

