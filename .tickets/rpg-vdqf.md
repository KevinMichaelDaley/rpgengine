---
id: rpg-vdqf
status: open
deps: [rpg-1sjm]
links: []
created: 2026-02-26T04:30:12Z
type: task
priority: 2
assignee: KMD
parent: rpg-b5ma
tags: [editor, polish, client, controller]
---
# Grab mode with client-side provisional positioning

Implement grab mode for interactive entity placement with zero-latency visual feedback.

READ FIRST: ref/editor_design.md §4.5 for grab mode design (client-side provisional positioning, editor_grab_state_t), ref/editor_ux.md §5.3 for grab workflow.

Requirements:
- Controller sends grab_begin to client (via client state socket)
- Client stores entity's authoritative position as grab_origin
- Client locally overrides rendered position to match cursor movement (zero latency)
- No server traffic during grab — purely local rendering
- Axis constraint: press x/y/z to lock movement to single axis
- Enter confirms: controller sends single 'move' command to server with final delta
- Escape cancels: client snaps entity back to grab_origin
- If server rejects move, entity reverts to authoritative position

Files to create:
- src/editor/client/client_grab_mode.c
- include/ferrum/editor/client/client_grab_mode.h
- src/editor/controller/ctrl_grab.c
- tests/editor/client_grab_mode_tests.c


## Notes

**2026-03-13T04:09:34Z**

Scope reduced: basic G grab + axis constraint (x/y/z lock) moved to Phase 1 §1.4 (rpg-1sjm). This ticket now covers ONLY the Phase 5 enhancement: client-side provisional positioning (zero-latency local rendering during grab, no server traffic until confirm). Depends on Phase 1 grab being implemented first.
