---
id: rpg-66ij
status: open
deps: []
links: []
created: 2026-02-26T04:30:12Z
type: task
priority: 2
assignee: KMD
parent: rpg-b5ma
tags: [editor, polish, controller]
---
# Full keybinding system (bind/unbind/save/load)

Implement the configurable keybinding system.

READ FIRST: ref/editor_ux.md §9 for keybinding syntax, default bindings, key sequence format, ref/editor_design.md §2.4 for bind command.

Requirements:
- cmd_bind: bind <key-sequence> <command>
- cmd_unbind: remove binding
- Key sequence syntax: single keys, modifiers (ctrl/alt/shift), sequences (g x), function keys
- Default bindings loaded on startup (hardcoded, overridable)
- bindings save <path>: save current bindings to .ini file
- bindings load <path>: load bindings from file
- Keybinding lookup: trie-based for multi-key sequences (g x needs prefix matching)
- Numeric prefix accumulation: digits accumulate, non-digit dispatches with repeat count

Files to create:
- include/ferrum/editor/ctrl_keybind.h
- src/editor/controller/ctrl_keybind.c
- src/editor/controller/ctrl_keybind_defaults.c
- tests/editor/ctrl_keybind_tests.c

