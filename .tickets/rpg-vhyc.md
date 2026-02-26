---
id: rpg-vhyc
status: open
deps: []
links: []
created: 2026-02-26T04:30:48Z
type: task
priority: 3
assignee: KMD
parent: rpg-l5le
tags: [editor, advanced, server]
---
# Entity search command

Implement the search command for finding entities by name, type, or component values.

READ FIRST: ref/editor_design.md §2.4 (cmd_search in dispatch table), ref/editor_ux.md §5.1 (/ key = search).

Requirements:
- cmd_search <pattern>: search entities by name/type/component
- Supports glob patterns: 'wall_*', '*_pillar'
- Supports component queries: 'physics.mass>10', 'render.mesh=pillar.glb'
- Results shown in log area, numbered like browse results
- Incremental search: results update as user types (when using / in normal mode)
- Selection integration: search results can be selected

Files to create:
- src/editor/commands/cmd_search.c
- src/editor/controller/ctrl_search.c
- tests/editor/cmd_search_tests.c

