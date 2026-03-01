---
id: rpg-7x16
status: closed
deps: [rpg-jyx3, rpg-qdn7]
links: []
created: 2026-03-01T05:36:00Z
type: task
priority: 2
assignee: KMD
parent: rpg-p9zq
tags: [editor, scripting]
---
# Math/vec3/quat scripting bindings

Implement scripting bindings for vec3 and quaternion math operations used by gameplay scripts.

Requirements:
- vec3 userdata type with metamethods: __add, __sub, __mul (scalar), __unm, __len (magnitude), __tostring
- vec3.new(x,y,z), vec3.dot(a,b), vec3.cross(a,b), vec3.normalize(v), vec3.lerp(a,b,t)
- quat userdata type with metamethods: __mul (compose), __tostring
- quat.new(w,x,y,z), quat.from_euler(x,y,z), quat.to_euler(q), quat.slerp(a,b,t), quat.rotate(q,v)
- Register as global tables in sandbox (math extensions, not replacements)
- All operations must be allocation-free (use stack userdata, not heap)

Files to create:
- src/editor/script/script_vec3.c (vec3 type + metamethods)
- src/editor/script/script_quat.c (quat type + metamethods)
- tests/editor/script_math_bindings_tests.c


## Notes

**2026-03-01T09:58:49Z**

Superseded by Aegis VM implementation. See ref/aegis_bytecode_spec.md.
