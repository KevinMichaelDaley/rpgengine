---
id: rpg-ohj4
status: open
deps: [rpg-talr, rpg-j8qf, rpg-l781, rpg-6p1o]
links: []
created: 2026-02-26T04:29:27Z
type: task
priority: 2
assignee: KMD
parent: rpg-vtza
tags: [editor, texsynth, server]
---
# Texsynth editor commands

Implement the texsynth command family in the edit protocol.

READ FIRST: ref/editor_design.md §2.4 dispatch table (cmd_texsynth), ref/editor_ux.md §7.1 for interactive synthesis workflow.

Requirements:
- texsynth new <name> <w> <h>: create workspace
- texsynth layer <name> <type> [params]: add noise/color layer
- texsynth blend <mode> <a> <b> <factor>: blend layers
- texsynth colorize <layer> <lo_rgb> <hi_rgb>: colorize grayscale
- texsynth normal_from_height <layer> <strength>: generate normal map
- texsynth preview: update preview in client viewport
- texsynth bake <name> <mesh_path> [--uv N] [--res N]: bake to files
- texsynth save <path>: save workspace definition
- All sub-commands routed through single cmd_texsynth handler

Files to create:
- src/editor/commands/cmd_texsynth.c
- tests/editor/cmd_texsynth_tests.c

