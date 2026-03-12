---
id: rpg-26lj
status: open
deps: [rpg-c6o7]
links: []
created: 2026-03-12T06:48:52Z
type: task
priority: 2
assignee: KMD
parent: rpg-0n7d
---
# §5.3 Musculature Installation

See ref/scene_editor_design.md §5.3. Inspector: muscle enable toggle, flexor/extensor sub-panels. All muscle parameters (max_force, optimal_length, max_velocity, pennation, tau_rise/fall, tendon slack/stiffness, wrap radius). Auto-fill from bone geometry. Viewport: line from origin to insertion, wireframe cylinder for wrap. Color coding (blue=relaxed, red=max).

## Acceptance Criteria

Muscle drive inspector with all parameters. Auto-fill works. Attachment lines and wrap cylinders render. Cross-reference with ref/scene_editor_spec.md and ref/scene_editor_ux.md for requirements and interaction details.

