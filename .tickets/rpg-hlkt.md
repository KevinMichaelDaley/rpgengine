---
id: rpg-hlkt
status: open
deps: [rpg-o8pq]
links: []
created: 2026-03-01T09:58:49Z
type: task
priority: 2
assignee: KMD
parent: rpg-ssr8
tags: [aegis, vm, tracing]
---
# Aegis execution trace construction

Implement per-execution trace capture per ref/aegis_bytecode_spec.md §5.1, §5.2.

Traces record what a script did during execution for security analysis.

Implement:
- aegis_trace_t: trace container (ring buffer of trace entries)
- Trace entry types: EVENT_PROCESSED, ENTITY_QUERIED, ATTR_READ, COMPUTED, UPDATE_WROTE, YIELDED
- aegis_trace_begin(trace): reset for new execution
- aegis_trace_record_query(trace, entity_id, key): log entity attribute read
- aegis_trace_record_update(trace, entity_id, key, value): log state mutation
- aegis_trace_record_event(trace, event_type): log event processing
- aegis_trace_hash(trace): compute content-addressable hash (uint64) of the trace
- aegis_get_trace(script): return trace of last execution

Trace recording is called by the VM's entity query and update handlers (hooks into get_attr, push_update, event_type, etc.). Storage is C struct ring buffers, not a graph database.

Files:
- include/ferrum/aegis/aegis_trace.h
- src/aegis/aegis_trace.c
- tests/aegis/aegis_trace_tests.c

Acceptance criteria:
- [ ] Traces record entity queries, attribute reads, updates, events
- [ ] Trace hash is deterministic (same execution → same hash)
- [ ] Different executions produce different hashes
- [ ] Ring buffer wraps correctly on overflow
- [ ] aegis_get_trace returns valid trace after execution
- [ ] Tests: record various entry types, hash determinism, hash differs for different traces, ring overflow

## Acceptance Criteria

Execution traces capture query/update patterns with deterministic content-addressable hash

