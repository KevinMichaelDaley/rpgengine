---
id: rpg-077b
status: open
deps: []
links: []
created: 2026-03-01T09:58:49Z
type: task
priority: 1
assignee: KMD
parent: rpg-8hc1
tags: [aegis, engine, events]
---
# Server-side event queue infrastructure

Implement the server-side event queue and topic subscription system required by the Aegis VM event-driven model. See ref/aegis_bytecode_spec.md §2.1, §2.2.

This is NEW engine infrastructure (no existing event queue covers this use case). The event queue routes typed events from engine subsystems (physics impacts, entity lifecycle, gameplay triggers) to subscribed script instances.

Implement:
- aegis_event_t: event struct with type hash, source entity_id, tick, payload (see §2.2)
- aegis_event_queue_t: per-script pending event queue (ring buffer, fixed capacity)
- aegis_topic_table_t: topic name → subscriber list mapping
- aegis_topic_subscribe(table, topic_hash, script_id): register script for events on topic
- aegis_topic_unsubscribe(table, topic_hash, script_id): remove subscription
- aegis_topic_publish(table, event): route event to all subscribers' queues
- aegis_event_queue_pop(queue, out_event): dequeue next event for a script

Topic naming: events use ! prefix (e.g., !hit, !behave, !spawn). Topic hashes are computed from the string name.

Integration point: the server tick loop's on_drain stage should drain engine events and call aegis_topic_publish. Physics impact events (phys_impact_event_t), entity spawn/despawn events, and gameplay triggers all publish to the event queue.

Files:
- include/ferrum/aegis/aegis_event.h (event type, queue, topic table)
- src/aegis/aegis_event_queue.c (queue init, push, pop)
- src/aegis/aegis_topic_table.c (subscribe, unsubscribe, publish/route)
- tests/aegis/aegis_event_tests.c

Acceptance criteria:
- [ ] Events routed correctly to subscribed scripts
- [ ] Multiple scripts can subscribe to same topic
- [ ] Scripts can subscribe to multiple topics
- [ ] Queue overflow drops oldest events (configurable)
- [ ] Topic hash computed correctly from ! prefixed name
- [ ] Tests: subscribe/publish/pop round-trip, multiple subscribers, multiple topics, queue overflow, unsubscribe

## Acceptance Criteria

Event queue routes typed events to subscribed scripts by topic, overflow handled

