---
id: rpg-za2w
status: closed
deps: [rpg-h84i, rpg-077b, rpg-rblb]
links: []
created: 2026-03-01T21:27:43Z
type: task
priority: 1
assignee: KMD
parent: rpg-p9zq
tags: [aegis, vm, runtime, server]
---
# Aegis script loader and fiber-based runtime

Implement the server-side script loader and fiber-based runtime that compiles IL scripts, launches each in its own fiber, and drives execution via the event queue.

This bridges the Aegis VM to the server tick loop. Each loaded script gets:
1. Its own aegis_vm_t instance
2. Its own aegis_event_queue_t for subscribed events
3. A fiber on the script runtime worker threads (dispatched via job_dispatch)

Components:

aegis_script_instance_t: per-script state
- aegis_vm_t vm (initialized from compiled bytecode)
- aegis_event_queue_t event_queue
- uint32_t script_id (index in instance table)
- char name[64] (debug name)
- bool active

aegis_script_runtime_t: runtime manager
- aegis_script_instance_t *instances (fixed-capacity array)
- aegis_topic_table_t topics (shared subscription routing)
- uint32_t instance_count, instance_cap
- Arena/pool for VM memory backing

API:
- aegis_script_runtime_init(rt, config): allocate instance slots, topic table
- aegis_script_runtime_load(rt, name, bytecode): create instance, auto-subscribe .topic, return script_id
- aegis_script_runtime_unload(rt, script_id): teardown instance, unsubscribe topics
- aegis_script_runtime_tick(rt, job_sys, events): for each pending event, pop into script queues via topic_publish, then dispatch each script with pending events as a fiber job
- aegis_script_runtime_destroy(rt): cleanup all instances

Fiber execution model (long-lived fiber per script, NOT per-tick dispatch):
- aegis_script_runtime_load() calls job_dispatch(script_fiber_fn, instance) ONCE
- script_fiber_fn is a long-lived loop that runs for the lifetime of the script:
    1. Pop next event from instance's event queue
    2. If no event pending, job_yield() to park until next tick pushes events
    3. Set vm->event to the popped event
    4. Call aegis_vm_run() in a loop:
       - YIELDED: event fully processed, go back to step 1
       - FORCE_YIELDED: fuel exhausted mid-execution, call job_yield() to let
         other fibers run, then refuel and call aegis_vm_run() again to resume
       - WAIT_YIELDED: async op pending, call job_yield(), then resume
       - EXITED: script terminated, mark inactive, fiber returns (exits fiber)
       - ERROR: log error, mark inactive, fiber returns
- The fiber scheduler handles all work distribution — no per-tick re-dispatch
- Force-yield maps directly to job_yield(), giving the engine natural preemption

Integration with server tick loop:
- on_drain stage: call aegis_script_runtime_tick() which:
  1. Routes incoming events to script queues via topic_publish
  2. Does NOT dispatch fibers (they are already alive and parked via job_yield)
  3. Signals/wakes parked script fibers so they resume processing
- Scripts that exited are reaped during tick (cleanup inactive instances)
- Publishes physics impact events, entity lifecycle events to topic table

Files:
- include/ferrum/aegis/aegis_runtime.h
- src/aegis/aegis_runtime_init.c (init, destroy)
- src/aegis/aegis_runtime_load.c (load, unload)
- src/aegis/aegis_runtime_tick.c (tick, fiber dispatch)
- tests/aegis/aegis_runtime_tests.c

Acceptance criteria:
- [ ] Scripts compiled from IL and loaded into runtime
- [ ] Each script gets own VM + event queue
- [ ] .topic directive auto-subscribes on load
- [ ] Events routed to correct scripts via topic table
- [ ] Each script launched as a SINGLE long-lived fiber (not re-dispatched per tick)
- [ ] Force-yield (fuel exhaustion) maps to job_yield() for natural preemption
- [ ] Fiber resumes VM from same PC after job_yield() returns
- [ ] Exited scripts cleaned up and unsubscribed
- [ ] Multiple scripts can run concurrently
- [ ] Tests: load/unload lifecycle, event routing through runtime, multi-script concurrent execution

