---
id: rpg-vebd
status: open
deps: [rpg-jyx3]
links: []
created: 2026-03-01T06:47:03Z
type: task
priority: 1
assignee: KMD
parent: rpg-p9zq
tags: [editor, scripting, security]
---
# Safe engine API: rate-limited log + validated entity writes

Replace print() and all external-state-manipulating Lua globals with safe engine API functions.

## Remove from sandbox
- print, rawset, rawget, setmetatable, getmetatable (added to script_sandbox_init strip list)

## Add engine.log / engine.warn / engine.err
- Rate-limited via token bucket (configurable, default 100/sec)
- engine.log(msg) → stdout with [SCRIPT] prefix
- engine.warn(msg) → stderr with [SCRIPT WARN] prefix  
- engine.err(msg) → stderr with [SCRIPT ERR] prefix
- Returns false + message when rate limit exceeded (does not error)

## Add engine.write_entity(id, key, type_str, value)
- Validates entity_id against snapshot bounds
- Validates key is uint16 in range
- Validates type_str maps to valid SCRIPT_ATTR_* enum
- Validates value size <= 255 bytes
- Calls script_env_write_attr internally
- Returns true on success, false + reason on validation failure

## Input validation (bounds guards)
- All string args: length-checked before extraction
- All numeric args: range-checked
- Stack depth protection on C callbacks

## Files
- include/ferrum/editor/edit_script_api.h (types: script_log_state_t)
- src/editor/script/edit_script_api.c (register, log_state_init)
- src/editor/script/edit_script_api_log.c (engine.log/warn/err callbacks)
- src/editor/script/edit_script_api_entity.c (engine.write_entity callback)
- tests/editor/edit_script_api_tests.c

