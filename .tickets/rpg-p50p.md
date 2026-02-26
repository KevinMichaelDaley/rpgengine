---
id: rpg-p50p
status: open
deps: [rpg-d6j3]
links: []
created: 2026-02-26T04:27:44Z
type: task
priority: 2
assignee: KMD
parent: rpg-tiet
tags: [editor, assets, server]
---
# Material assignment commands

Implement material set/get commands for assigning textures/materials to entities.

READ FIRST: ref/editor_design.md §2.4 dispatch table (cmd_material entry), ref/editor_ux.md §7.3 for material workflow in texture synthesis context.

Requirements:
- cmd_material set <entity> <slot> <path>: assign material/texture to entity's material slot
- cmd_material get <entity>: query current material assignments
- Undo support (material change records old material for reversal)
- Triggers asset download on client if material not cached
- Material slots: albedo, normal, roughness, metallic, emissive

Files to create:
- src/editor/commands/cmd_material.c
- tests/editor/cmd_material_tests.c

