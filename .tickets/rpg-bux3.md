---
id: rpg-bux3
status: open
deps: [rpg-ynd4]
links: []
created: 2026-07-04T20:39:26Z
type: task
priority: 0
assignee: KMD
parent: rpg-8sc6
tags: [procgen, critic, hooks]
---
# procgen-5b: Hook registration/unregistration API

## Design

Implement critic_hook_register() and critic_hook_unregister() in critic/critic_hooks.c. Registry is a dynamic array of hook pointers. critic_hook_fire() iterates all registered hooks and calls each callback with the event. Thread-safe via mutex or atomic operations (hooks are read-heavy during play). Write RED tests for register/unregister/fire behavior.

## Acceptance Criteria

- Multiple hooks can be registered\n- Hooks can be unregistered by pointer\n- Fire calls all registered hooks\n- Fire with no hooks is a no-op\n- Registration after unregistration of same pointer works\n- Thread-safe for concurrent fire calls

