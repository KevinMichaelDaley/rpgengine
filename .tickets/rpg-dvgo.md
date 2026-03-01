---
id: rpg-dvgo
status: open
deps: []
links: []
created: 2026-03-01T05:35:04Z
type: task
priority: 2
assignee: KMD
parent: rpg-p9zq
tags: [editor, scripting, ecs]
---
# Dynamic entity attribute storage (entity_attrs_t)

Implement entity_attrs_t — a fixed-capacity (2048 byte) dynamic key-value attribute block that lives on both edit entities and ECS entities.

READ FIRST: ref/editor_design.md §6.2 for entity_attrs_t layout, attr_entry_t, and key ranges.

This is the foundation for gameplay scripts to store arbitrary per-entity state (health, velocity, AI flags, custom properties) without modifying entity structs.

Requirements:
- entity_attrs_t struct with sorted attr_entry_t directory + packed payload
- ENTITY_ATTRS_CAPACITY = 2048 bytes total budget per entity
- attr_entry_t: key (u16), type (u8), size (u8), offset (u16)
- Binary search by key for O(log n) lookups
- entity_attrs_init(attrs) — zero-initialize
- entity_attrs_set(attrs, key, type, data, size) → bool — insert/update attribute, returns false if out of space
- entity_attrs_get(attrs, key, out_type, out_size) → const void* — lookup by key, NULL if absent
- entity_attrs_remove(attrs, key) → bool — remove and compact
- entity_attrs_clear(attrs) — remove all
- entity_attrs_count(attrs) → uint16_t
- Add entity_attrs_t field to edit_entity_t (include/ferrum/editor/edit_entity.h)
- Add entity_attrs_t as ECS component (registered sparse set)
- Well-known key enums: SCRIPT_KEY_POS=0 through SCRIPT_KEY_MATERIAL=6, SCRIPT_KEY_ECS_BASE=64, SCRIPT_KEY_USER=256
- Attribute types: SCRIPT_ATTR_F32, VEC3, I32, U32, BOOL, STR, BLOB

Files to create:
- include/ferrum/entity/entity_attrs.h (entity_attrs_t, attr_entry_t, key/type enums)
- src/entity/entity_attrs.c (init, set, get, remove, clear, count)
- src/entity/entity_attrs_search.c (binary search helper)
- tests/entity/entity_attrs_tests.c

