---
id: rust-rpg-1vx
status: open
deps: [rust-rpg-a8y]
links: []
created: 2026-01-17T18:04:54.780714178-08:00
type: epic
priority: 2
---
# P_013 — AI & Scripting (Dynamic Modules, BT, HTN, Hierarchical Pathfinding)

## P_013 — AI & Scripting (Dynamic Modules, BT, HTN, Hierarchical Pathfinding)

### Design Intent
Provide a modular, server-authoritative AI system with runtime-loaded "scripting" via dynamic libraries (e.g., `dlopen`/`LoadLibrary`), enabling behavior trees (BT), hierarchical task network (HTN) planning, and hierarchical pathfinding. Support composable gameplay behaviors and abilities, and integrate cleanly with the quest system for deterministic, debuggable behavior.

### Specification
#### Dynamic Modules ("Scripting")
- Loader abstracts `dlopen` (POSIX) and `LoadLibrary` (Windows) with a common interface.
- Module ABI (versioned): required entry points
  - `module_init(api*)` returns `module_handle` or error.
  - `module_tick(ctx*)` for per-frame or per-agent updates.
  - `module_shutdown(module_handle)` for cleanup.
- Registration: modules register behaviors, abilities, planners via function tables.
- Safety: version checks, symbol resolution validation, and error returns; no unchecked casts.

#### Behavior Trees (BT)
- Node types: `Sequence`, `Selector`, `Parallel`, `Decorator` (Invert/Repeat), `LeafAction`.
- Blackboard: per-agent key-value store (POD, fixed capacity) with typed accessors.
- Tick semantics: nodes return `Success`, `Failure`, or `Running`.
- ECS integration: agents have `bt_component` referencing tree definition and runtime cursor state.
- Job scheduling: per-agent BT ticks run as jobs with chunking to avoid long frames.

#### HTN Planning
- Domain: tasks (compound/primitive), methods, and operators with preconditions/effects.
- Planner builds a plan given current world state (read-only snapshot) and goals.
- Replanning triggers: failure on execution, significant state changes, or time budget exceeded.
- Plan cache: memoize recent plans for similar states; invalidate on key changes.

#### Hierarchical Pathfinding
- Multi-level grid/navmesh decomposition (coarse zones → fine cells).
- High-level A* across zones; local path refinement within cells using A* or flow-guided steering (see P_011).
- Agent path component stores waypoints; steering system consumes waypoints and physics applies movement.

#### Abilities & Modular Behaviors
- Ability definitions (from modules) with conditions, cooldowns, resource costs, and effects.
- Abilities exposed as BT leaf actions and HTN operators.
- Deterministic cooldown/resource counters; ECS components track states.

#### Quest System Integration
- BT/HTN preconditions can reference quest states (via read-only view).
- Actions can emit quest-related events; integration uses reliable ordered channel (P_007) for networking.
- Deterministic traces: record BT/HTN decisions and quest events in debug mode.

#### Networking (Summary)
- Server authoritative AI state; clients replicate coarse AI state (current behavior/goal) for UI.
- Reliability policies align with P_007: behavior/goal changes reliable; minor state like timers may be unreliable.

### Implementation Steps
1. Module loader abstraction and versioned ABI; error-handling paths.
2. Behavior tree engine: node structs, tick functions, blackboard API.
3. HTN planner: domain structs, precondition/effect evaluation, planner algorithm.
4. Hierarchical pathfinding: zone graph build, A* high-level, local refinement.
5. ECS components: `bt_component`, `htn_component`, `path_component`, `ability_component`.
6. Job scheduling for BT ticks, planning, and path updates; budgets per frame.
7. Integration hooks for quest system events and predicates.
8. Networking glue for AI state replication (optional UI states).
9. Instrumentation: tick counts, planner time, nodes visited, path lengths.

### Architectural Considerations
- Determinism: BT/HTN evaluation should be pure against snapshots; avoid nondeterministic iteration orders.
- No per-tick mallocs in hot loops; use arenas and fixed-size blackboards.
- Module isolation: strict ABI, version checks, and clear ownership for resources.
- Thread safety: module `tick` may run on worker threads; forbid global mutable state unless isolated.
- Clean under `-Wall -Wextra -Wpedantic`; pure C11 plus platform loader APIs.

### Unit Tests (RED-first)
**Happy Path**
1. **Module load/unload**
   - dummy module loads via loader; init/tick/shutdown called; registration succeeds.
2. **BT sequence/selector semantics**
   - define simple trees; ticks yield expected `Success`/`Failure`/`Running` results.
3. **HTN produces valid plan**
   - given domain/goals, planner returns expected operator sequence.
4. **Hierarchical pathfinding route**
   - agent plans high-level path across zones and refines local path.
5. **Ability cooldown/resource update**
   - ability invoked updates cooldown/resource counters deterministically.
6. **Quest precondition gating**
   - BT/HTN preconditions referencing quest state gate actions appropriately.

**Edge Cases**
7. **Module version mismatch**
   - loading a module with incompatible version returns explicit error.
8. **BT decorator repeat/invert edge**
   - repeat count boundaries and invert semantics produce documented outcomes.
9. **HTN replanning triggers**
   - planner detects failed execution and replans.
10. **Unreachable path**
   - pathfinder reports no path; agent switches to fallback behavior.
11. **Blackboard capacity limit**
   - exceeding key capacity returns error; no overflow.

**Failure Modes**
12. **Module missing symbol**
   - loader fails to resolve required symbol; module rejected.
13. **Invalid ability definition**
   - malformed ability returns error; not registered.
14. **Planner precondition failure**
   - operator cannot apply; planner returns failure with explicit reason.

### Regression Tests (RED-first)
1. **BT tick determinism**
   - fixed tree and blackboard yield identical tick traces across runs.
2. **HTN plan stability**
   - given identical domain/state, resulting plan equals golden sequence.
3. **Module ABI stability**
   - function table layout and version checks locked by tests.
4. **Pathfinding performance budget**
   - nodes visited count stays under budget for scripted maps.

### Cumulative Integration Tests (RED-first, cumulative through P_013)
1. **AI drive movement (P_000..P_013)**
   - BT selects move-to behavior; HTN plans path; physics moves agent; verify deterministic positions/events.
2. **Quest-integrated behavior**
   - quests gate actions; AI emits events; quest log updates; dialogue options reflect new quest states.
3. **Networked AI state**
   - server replicates behavior/goal changes; client UI reflects; reconciliation stays consistent.
4. **Custom attributes manifest**
   - scripting module registers attributes; server replicates changes; client applies via manifest; AI decisions may read these attributes deterministically.

---



