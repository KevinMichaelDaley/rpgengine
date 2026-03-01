---
id: rpg-lqfl
status: closed
deps: [rpg-zqex]
links: []
created: 2026-03-01T05:35:50Z
type: task
priority: 2
assignee: KMD
parent: rpg-p9zq
tags: [editor, scripting, server]
---
# Native script function registry

Implement native C function registration for the script runtime — C functions that run on the script thread using the same script_env_t interface as scripts.

READ FIRST: ref/editor_design.md §6.4 for native code path and example.

Requirements:
- script_native_fn typedef: void (*)(script_env_t *env, void *userdata)
- script_runtime_register_native(rt, fn, userdata) — add to registry (max 32 native fns)
- script_runtime_unregister_native(rt, fn) — remove from registry
- Native functions execute every tick on the script thread, after scripts
- Native functions use the same script_env_t reads/writes as scripts
- Thread safety: registration must happen before runtime start or via atomic flag

Files to create:
- src/editor/script/edit_script_native.c (register, unregister, run_all)
- tests/editor/edit_script_native_tests.c


## Notes

**2026-03-01T09:58:48Z**

Superseded by Aegis VM implementation. See ref/aegis_bytecode_spec.md.
