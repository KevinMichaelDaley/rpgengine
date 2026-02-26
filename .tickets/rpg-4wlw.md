---
id: rpg-4wlw
status: open
deps: [rpg-ohj4]
links: []
created: 2026-02-26T04:29:27Z
type: task
priority: 2
assignee: KMD
parent: rpg-vtza
tags: [editor, texsynth, scripting]
---
# Lua texture synthesis API bindings

Implement Lua bindings for the texture synthesis system.

READ FIRST: ref/editor_ux.md §8.3 for texture API (texsynth.new, ws:layer, ws:blend, ws:bake, etc.).

Requirements:
- texsynth.new(w, h) → workspace userdata
- ws:layer(name, type, params_table) → add layer
- ws:blend(op, a, b, factor) → blend layers
- ws:colorize(layer, lo_color, hi_color) → colorize
- ws:normal_from_height(layer, strength)
- ws:bake(name, mesh_path, opts_table) → bake to files
- ws:save(path) → save workspace
- Workspace is Lua userdata with __gc metamethod for cleanup
- Params tables map to C noise/blend parameters

Files to create:
- src/editor/script/lua_texsynth_api.c
- tests/editor/lua_texsynth_api_tests.c

