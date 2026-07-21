---
id: rpg-uhhr
status: closed
deps: [rpg-vwyk, rpg-iplq, rpg-96ia]
links: []
created: 2026-07-21T01:35:45Z
type: task
priority: 2
assignee: KMD
parent: rpg-k23d
tags: [renderer, perf, config]
---
# Ship low/med/high renderer preset JSONs

Section 7.3. render_config_parse is a clean overlay parser -- ship three JSON files (low/med/high) overlaid before the per-level config; no code beyond adding the fields. Populate from the section 7.3 tables (existing + new keys). Note dir_cascades=0 and shadow_res=0 already work as full-off switches (render_forward.c:134,150,227,243), so the ultra-low rung needs no new code.

## Acceptance Criteria

low/med/high preset JSONs load via the overlay parser and select the section 7.3 values; a quality selection visibly trades fidelity for frame time.


## Notes

**2026-07-21T01:49:03Z**

DONE (JSON deliverable). Shipped assets/config/render_config.{low,med,high}.json from the section 7.3 tables (perf/quality keys only, so they compose with a level's look keys). Selectable standalone today via the same path mechanism as render_xe.json (render_config_load); once overlay-chaining (preset THEN level config) lands they merge cleanly. default.json updated with all new keys. Load-tested: render_config_tests::preset_files_load. NOTE: the visible frame-time delta scales in as each consumer ticket (render_scale, cube-shadow budget, fp16, shader variants, ...) lands -- many keys currently parse but are not yet read.
