---
id: rpg-f77m
status: open
deps: [rpg-po47, rpg-kjmx]
links: []
created: 2026-03-12T06:48:53Z
type: task
priority: 2
assignee: KMD
parent: rpg-o25i
---
# §6.4 Playback State Machine

See ref/scene_editor_design.md §6.4. Play (step local runner, advance playhead). Pause. Step (one tick). Rewind (reset local physics). Fast-forward sync and async (:anim ff --async). Record (capture sim output as baked keyframes). Loop (repeat time selection range).

## Acceptance Criteria

Play/pause/step/rewind/ff/loop all work. Record captures sim output. Cross-reference with ref/scene_editor_spec.md and ref/scene_editor_ux.md for requirements and interaction details.

