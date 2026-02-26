---
id: rpg-18cm
status: open
deps: [rpg-d6j3, rpg-89t3]
links: []
created: 2026-02-26T04:27:44Z
type: task
priority: 2
assignee: KMD
parent: rpg-tiet
tags: [editor, assets, controller]
---
# Browse command with #N references

Implement the browse command for structured asset browsing with numbered references.

READ FIRST: ref/editor_design.md §9.6 for browse result caching (ctrl_browse_t), ref/editor_ux.md §6.2 for browse command UX, ref/editor_ux.md §3.3 for #N reference syntax.

Requirements:
- cmd_browse on server: list directory contents from asset registry, supports --filter and --sort
- Controller displays numbered results in log area: [1] pillar.glb [2] wall_section.glb ...
- Controller caches results in ctrl_browse_t so user can use #N syntax
- #N expansion: 'spawn mesh #2' expands to 'spawn mesh wall_section.glb'
- References valid until next browse command
- Max browse results: configurable (default 100)

Files to create:
- src/editor/controller/ctrl_browse.c
- src/editor/commands/cmd_browse.c (server-side)
- tests/editor/ctrl_browse_tests.c

