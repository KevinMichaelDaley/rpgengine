---
id: rpg-rblb
status: open
deps: [rpg-077b, rpg-o8pq]
links: []
created: 2026-03-01T09:58:49Z
type: task
priority: 1
assignee: KMD
parent: rpg-8hc1
tags: [aegis, vm, events]
---
# Aegis event access instructions

Implement event access bytecode instructions per ref/aegis_bytecode_spec.md §3.3.

Instructions:
- event_type r_dst: load event type hash (uint32) into register
- event_src r_dst: load source entity ID (uint32) into register
- event_field r_dst, off, type: load typed field from event payload at byte offset; bounds-checked against payload_len; type determines read size (SCRIPT_ATTR_F32=4 bytes, SCRIPT_ATTR_VEC3=12 bytes, etc.)

Also implement .topic directive handling:
- At bytecode load time, the script's topic hash is extracted from the .topic directive
- The VM auto-subscribes the script to its declared topic via aegis_topic_subscribe()
- On each aegis_resume(), the next event is popped from the script's event queue and made available to event_type/event_src/event_field instructions

Files:
- src/aegis/ops/aegis_ops_event.c (event_type, event_src, event_field handlers)
- tests/aegis/aegis_ops_event_tests.c

Acceptance criteria:
- [ ] event_type loads correct hash value
- [ ] event_src loads correct entity ID
- [ ] event_field reads correct typed data at offset (f32, vec3, i32, etc.)
- [ ] event_field rejects out-of-bounds offset (offset + type_size > payload_len)
- [ ] .topic directive correctly subscribes script on load
- [ ] Tests: access all event fields, payload boundary check, type-size mismatch

## Acceptance Criteria

Event instructions read event data correctly with bounds-checked payload access

