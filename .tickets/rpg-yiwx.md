---
id: rpg-yiwx
status: open
deps: []
links: [rpg-1sjm]
created: 2026-03-15T07:54:10Z
type: task
priority: 1
assignee: KMD
tags: [engine, physics, renderer, editor]
---
# Engine-wide pivot offset support

## Overview

Every entity has a `pivot_offset[3]` in local space. The model-space origin is always the pivot: `pos` maps to the pivot world position and geometry is offset by `-pivot_offset`. The model matrix is `T(pos) * R(orientation) * S(scale) * T(-pivot_offset)`. All engine subsystems must treat the pivot as the object origin — rotation and scale happen around the pivot, physics bodies are positioned with the pivot as their center, and the renderer draws geometry offset from the pivot.

Currently `pivot_offset` exists on `edit_entity_t` and is serialized/deserialized, but only the editor scene viewport renderer accounts for it. The physics engine, physics bridge, replication, and editor commands all treat `pos` as the geometry center rather than the pivot point.

## Semantic change

- `entity.pos` = **pivot world position** (where the gizmo renders, where rotation/scale happen)
- Geometry center in world space = `pos + R * S * (-pivot_offset)`
- When `pivot_offset` is `(0,0,0)`, everything reduces to current behavior

## Changes required

### 1. Physics body position (`src/physics/`)

**`phys_body_t`** does not need a pivot field. Instead, the physics bridge translates between editor-space (pivot-centric) and physics-space (geometry-centric) when creating and syncing bodies.

- **Body creation** (`src/physics/world/phys_cmd_drain.c` `apply_spawn_`): when spawning a body from an editor entity, compute the geometry center: `body.position = entity.pos + rotate(entity.orientation, scale_vec(entity.scale, -entity.pivot_offset))`. Collider offset stays at `(0,0,0)` (centered on the body).
- **Position sync** (`apply_set_position_`): when the editor moves an entity (sends pos = pivot world position), compute body position the same way.
- **Reverse sync** (physics → editor for simulation playback): `entity.pos = body.position - rotate(body.orientation, scale_vec(entity.scale, -entity.pivot_offset))`.

### 2. Physics bridge callbacks (`include/ferrum/editor/edit_cmd_ctx.h`)

- `on_spawn()` already receives the full `edit_entity_t*` — it has `pivot_offset`. The bridge implementation must offset body position.
- `on_move()` currently passes raw `pos[3]`. Either:
  - (a) Also pass `pivot_offset` and `orientation` so the bridge can compute body position, or
  - (b) Pass the computed geometry center directly. Option (a) is cleaner since the bridge can decide how to handle it.
- Add `on_pivot_change()` callback for when pivot_offset is modified (needs to reposition the body without moving the visual entity).

### 3. Editor commands (`src/editor/commands/`)

**`cmd_move.c` / `cmd_move_id.c`**: No change needed. These modify `entity.pos`, which is the pivot position. The bridge callback handles the physics body offset.

**`cmd_rotate.c` / `cmd_rotate_id.c`**: No change needed. Rotation changes `entity.orientation`. Since the model matrix applies rotation around the pivot (at `pos`), and the physics bridge recomputes body position from the new orientation + pivot, the body position naturally updates.

**`cmd_scale.c` / `cmd_scale_id.c`**: No change needed for the entity. The physics bridge must recompute body position from the new scale + pivot.

**`cmd_pivot_id.c` (NEW)**: Set `pivot_offset` on an entity by ID. Args: `{"entity_id": N, "pivot": [x, y, z]}`. Must also adjust `entity.pos` so the geometry stays in place: `pos_new = pos_old + R * S * (pivot_new - pivot_old)`. Calls bridge `on_pivot_change()` to reposition the physics body. Version-stamps the entity.

### 4. Renderer model matrix (already done)

`scene_viewport_draw.c` `build_model_matrix()`: `T(pos) * R * S * T(-pivot_offset)`. Already implemented.

### 5. Editor gizmo (`src/editor/scene/`)

**Gizmo position**: For single selection, gizmo position = `entity.pos` (the pivot world position). No need to compute `pos + R * pivot_offset` since pos IS the pivot.

**Rotation**: No orbit logic needed for entity pivot. The model matrix handles it. Cursor-basis orbit (multiple entities around 3D cursor) is unrelated and stays.

**Pivot edit mode**: When dragging the pivot, adjust both `pivot_offset` (local-space delta) AND `pos` (world-space delta) simultaneously so geometry stays in place. On drag end, send `pivot_id` command to server. Alt+G (reset pivot): set `pivot_offset = (0,0,0)` and adjust `pos` by `R * S * old_pivot`.

### 6. Entity selection raycast (`scene_input.c`)

AABB pick candidates currently use `entity.pos` as center. With pivot, the geometry center is offset. Pick candidates should use the geometry world center: `pos + R * S * (-pivot_offset)`. Similarly for box select projection.

### 7. Network replication (`src/net/replication/`)

`body_state.c` encodes/decodes `body.position` — this is the physics body's geometry center, not the pivot. No change needed in the replication protocol. The editor-to-physics offset is internal to the server.

### 8. Serialization (already done)

`edit_serialize.c` / `edit_deserialize.c` already handle `pivot_offset`. The `sync_entities` delta protocol includes it in entity JSON.

## Files

| File | Change |
|------|--------|
| `src/physics/world/phys_cmd_drain.c` | Offset body position by pivot in `apply_spawn_`, `apply_set_position_` |
| `include/ferrum/editor/edit_cmd_ctx.h` | Extend `on_move` signature or add `on_pivot_change` callback |
| `src/editor/commands/cmd_pivot_id.c` | NEW — set pivot_offset + adjust pos, call bridge |
| `src/editor/commands/edit_commands_register.c` | Register `pivot_id` |
| `include/ferrum/editor/edit_commands.h` | Declare `cmd_pivot_id` |
| `src/editor/controller/ctrl_cmd_defs.c` | Add `pivot_id` TUI command def |
| `src/editor/scene/scene_viewport_draw.c` | Gizmo position = `pos` for single selection (simplify) |
| `src/editor/scene/scene_input.c` | Remove entity pivot orbit logic, fix pivot edit drag to also adjust pos, fix pick AABB centers |
| `tests/editor/cmd_pivot_id_tests.c` | Tests for pivot_id command |
| `tests/editor/pivot_physics_tests.c` | Tests for physics body offset with pivot |

## Acceptance criteria

- Setting a non-zero pivot_offset and rotating an entity visually rotates around the pivot in both the editor viewport and the physics simulation
- Scaling with a non-zero pivot scales around the pivot
- Moving an entity moves the pivot (and geometry follows)
- Editing the pivot (pivot edit mode drag, Alt+G reset, pivot_id command) keeps geometry in place
- Physics body position matches the geometry center, not the pivot
- Raycasting / box select still picks entities correctly with non-zero pivot
- Save/load preserves pivot_offset
- Network replication is unaffected (body positions are geometry-centric)
