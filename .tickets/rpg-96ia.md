---
id: rpg-96ia
status: closed
deps: []
links: []
created: 2026-07-21T01:35:45Z
type: task
priority: 1
assignee: KMD
parent: rpg-k23d
tags: [renderer, perf, config]
---
# Fix low-end-hostile renderer defaults (msaa, aniso, gi_bounce)

Section 7.1. Engine defaults in src/scene/render_config_defaults.c are hostile to iGPUs:
- msaa 4 -> 2 (0 on low): 4xMSAA on an HDR forward+ pass is a major fill cost on iGPUs.
- aniso 16 -> 4: 16x on EVERY material map (incl. normal/ORM) multiplies fetch cost.
- gi_bounce 0.9 -> 0.45: documented as settling at ~10x energy post multi-bounce fix (commit e1effa87); 0.9 is wrong energy for any level without a config AND slower convergence.
No new code -- just change the three default values (render_config_defaults.c:58 msaa/aniso, :54 gi_bounce).

## Acceptance Criteria

Defaults are msaa=2, aniso=4, gi_bounce=0.45; a level with no render config still renders correctly with the corrected GI energy.


## Notes

**2026-07-21T01:49:03Z**

DONE. render_config_defaults.c: msaa 4->2, aniso 16->4, gi_bounce 0.9->0.45. Pinned msaa:4/aniso:16 into datasets/great_hall_export/render.json so the discrete flagship is unchanged (it inherited msaa/aniso from defaults; gi_bounce was already 0.45 there). assets/config/render_config.default.json updated to mirror. Tests: render_config_tests::corrected_hostile_defaults (13/13 pass).
