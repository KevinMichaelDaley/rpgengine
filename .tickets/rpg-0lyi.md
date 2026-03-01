---
id: rpg-0lyi
status: open
deps: []
links: []
created: 2026-03-01T09:58:49Z
type: epic
priority: 1
assignee: KMD
parent: rpg-p9zq
tags: [aegis, vm, core]
---
# Aegis VM Phase 1: Core Interpreter

Core bytecode interpreter for the Aegis scripting VM. Implements the register file, instruction decoder, three-zone memory model, arithmetic/logic/control-flow/data-movement/vector instructions, yield/resume/exit coroutine lifecycle, fuel metering, and the main interpreter loop. See ref/aegis_bytecode_spec.md §3 and §6 for full specification.

