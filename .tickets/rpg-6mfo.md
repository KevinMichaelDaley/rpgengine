---
id: rpg-6mfo
status: open
deps: [rpg-f77m]
links: []
created: 2026-03-12T06:48:53Z
type: task
priority: 2
assignee: KMD
parent: rpg-o25i
---
# §6.5 Simulation Baking

See ref/scene_editor_design.md §6.5. anim_bake_config_t (target entities, frame range, step interval, channel mask). Skeletal record mode (Ctrl+R, local runner captures bone transforms). Rigid body bake (:anim bake sim, local runner for selected entities). Fracture bake (per-fragment keyframe tracks). Progress display. Post-bake cleanup (:anim bake clean, Douglas-Peucker). Bake commit to server. Baked keyframes editable like hand-authored.

## Acceptance Criteria

Record mode captures skeletal sim. :anim bake sim captures rigid body/fracture. Post-bake cleanup reduces keyframes. Baked keyframes appear in timeline and are editable. Cross-reference with ref/scene_editor_spec.md and ref/scene_editor_ux.md for requirements and interaction details.

