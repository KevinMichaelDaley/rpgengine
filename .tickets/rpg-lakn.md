---
id: rpg-lakn
status: open
deps: [rpg-zqex]
links: []
created: 2026-02-26T04:28:42Z
type: task
priority: 2
assignee: KMD
parent: rpg-p9zq
tags: [editor, scripting, server]
---
# Lua math/vector/quaternion bindings

Implement Lua bindings for math types: vec3, quat, noise functions.

READ FIRST: ref/editor_ux.md §8.3 for math API listing.

Requirements:
- vec3(x,y,z) constructor + arithmetic metamethods (+, -, *, /, unary -)
- vec3 methods: length, normalize, dot, cross
- quat(x,y,z,w) constructor
- quat_from_euler(rx, ry, rz) → quat
- noise.perlin(x, y, params) → number
- noise.simplex(x, y, params) → number
- cursor() → vec3 (current cursor position)
- set_cursor(pos) → nil
- grid_size() → number

Files to create:
- src/editor/script/lua_math_api.c
- src/editor/script/lua_noise_api.c
- tests/editor/lua_math_api_tests.c

