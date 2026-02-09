# Client Frame Call Graph & Data Flow

This document describes the client frame loop orchestration and data
flow between subsystems.  See `ref/client_architecture.md` for the
full design narrative and phased implementation plan.

Source of truth (existing modules):
- `src/net/client/runtime_rx_*.c` — RX thread (receive, ACK, reassemble)
- `src/net/replication/interp/pose_interpolator.c` — snapshot interpolation
- `src/renderer/*.c` — shader, VAO/VBO, render pipeline
- `src/renderer/skinning/*.c` — skeletal animation pipeline
- `src/renderer/debug_lines/*.c` — debug line segments

Public entry points (planned; not all exist yet):
```c
void client_frame(client_t *cl);                    /* main frame (to build)       */
bool fr_client_rx_start(fr_client_rx_t *rx);         /* RX thread                  */
bool fr_client_rx_pop_message(fr_client_rx_t *rx,    /* pop decoded message        */
     uint32_t channel, uint8_t *out, size_t *len);
void fr_pose_interpolator_push(...);                  /* push snapshot              */
bool fr_pose_interpolator_sample(...);                /* sample interpolated pose   */
int  render_pipeline_execute(const render_pipeline_t *p); /* run render passes     */
```

---

## High-Level Call Graph

```text
client_frame(cl)
  │
  ├─ Stage 1: poll_sdl_events(cl)
  │    └─ while (SDL_PollEvent(&ev)):
  │         ├─ SDL_QUIT           → cl->running = false
  │         ├─ SDL_KEYDOWN/UP     → input_key_set(&cl->input, scancode, down)
  │         ├─ SDL_MOUSEMOTION    → input_mouse_accum(&cl->input, xrel, yrel)
  │         └─ SDL_MOUSEBUTTONDOWN→ input_mouse_btn(&cl->input, button, true)
  │
  ├─ Stage 2: drain_network_messages(cl)
  │    ├─ /* reliable channel (stream-reassembled, ordered) */
  │    │  while (fr_client_rx_pop_message(cl->rx, CH_RELIABLE, buf, &len)):
  │    │    ├─ schema_id = read_u16_le(buf)
  │    │    └─ if schema_id == NET_REPL_SCHEMA_EVENT:
  │    │         reliable_event_queue_push(&cl->event_queue, buf+2, len-2)
  │    │       elif schema_id == NET_REPL_SCHEMA_WELCOME:
  │    │         client_handle_welcome(cl, buf+2, len-2)
  │    │
  │    └─ /* unreliable channel (latest-wins) */
  │       while (fr_client_rx_pop_message(cl->rx, CH_UNRELIABLE, buf, &len)):
  │         ├─ schema_id = read_u16_le(buf)
  │         └─ switch (schema_id):
  │              ├─ BODY_STATE  → state_update_push(&cl->state_buf, buf+2, len-2)
  │              └─ STATE_CUBE  → state_update_push(&cl->state_buf, buf+2, len-2)
  │
  ├─ Stage 3: process_reliable_events(cl)
  │    └─ while (reliable_event_queue_pop(&cl->event_queue, &event)):
  │         ├─ server_event_decode(raw, len, &event)
  │         └─ switch (event.type):
  │              ├─ SERVER_EVENT_SPAWN:
  │              │    ├─ ecs_world_create_entity(&cl->ecs, &entity)
  │              │    ├─ fr_pose_interpolator_reset(&interp)
  │              │    ├─ assign mesh (cube placeholder / asset_id lookup)
  │              │    └─ register in render world
  │              ├─ SERVER_EVENT_DESPAWN:
  │              │    ├─ remove from render world
  │              │    └─ ecs_world_destroy_entity(&cl->ecs, entity)
  │              ├─ SERVER_EVENT_DEATH:
  │              │    └─ trigger_death_fx(cl, entity, event.data.death.cause)
  │              ├─ SERVER_EVENT_HEALTH:
  │              │    └─ set_component(&cl->ecs, entity, health_c, event.data.health)
  │              ├─ SERVER_EVENT_STATUS:
  │              │    └─ set_component(&cl->ecs, entity, status_c, event.data.status)
  │              └─ SERVER_EVENT_INVENTORY:
  │                   └─ set_component(&cl->ecs, entity, inv_c, event.data.inventory)
  │
  ├─ Stage 4: apply_state_updates(cl)
  │    └─ while (state_update_pop(&cl->state_buf, &bs)):
  │         ├─ net_repl_body_state_decode(raw, len, &bs)
  │         ├─ interp = entity_get_interp(&cl->ecs, bs.body_id)
  │         ├─ pos = net_repl_vec3_mm_to_float(&bs.pos_mm)
  │         ├─ rot = net_repl_quat_decode(&bs)
  │         └─ fr_pose_interpolator_push(interp, now_s, pos, rot)
  │
  ├─ Stage 5: update_player_controller(cl)
  │    ├─ fps_controller_update(&cl->controller, &cl->input, dt)
  │    │    ├─ yaw   += input.mouse_dx * sensitivity
  │    │    ├─ pitch  = clamp(pitch + input.mouse_dy * sensitivity, -89, 89)
  │    │    ├─ forward = { -sin(yaw), 0, -cos(yaw) }
  │    │    ├─ right   = { cos(yaw), 0, -sin(yaw) }
  │    │    └─ move_vel = forward * input.fwd + right * input.strafe
  │    │
  │    └─ camera_from_controller(&cl->camera, &cl->controller)
  │         ├─ eye = controller.position + (0, eye_height, 0)
  │         ├─ view = mat4_look_at(eye, eye + look_dir, up)
  │         └─ proj = mat4_perspective(fov, aspect, near, far)
  │
  ├─ Stage 6: send_input_to_server(cl)
  │    ├─ if (now - cl->last_input_send >= input_interval):
  │    │    ├─ net_repl_input_move_encode(&move, buf, &len)
  │    │    ├─ net_rudp_peer_send_reliable(peer, sock, addr, now_ms,
  │    │    │       NET_REPL_SCHEMA_INPUT_MOVE, buf, len)
  │    │    ├─ net_repl_input_rot_encode(&rot, buf, &len)
  │    │    └─ net_rudp_peer_send_reliable(peer, sock, addr, now_ms,
  │    │         NET_REPL_SCHEMA_INPUT_ROT, buf, len)
  │    └─ cl->last_input_send = now
  │
  ├─ Stage 7: interpolate_poses(cl)
  │    └─ render_time = now_s - snapshot_interval_s
  │       for each entity with fr_pose_interpolator_t:
  │         ├─ fr_pose_interpolator_sample(interp, render_time,
  │         │       QUAT_EPS, &pos, &rot)
  │         └─ entity_set_transform(&cl->ecs, entity, pos, rot)
  │
  ├─ Stage 8: build_render_lists(cl)
  │    ├─ /* static / cube meshes */
  │    │  for each entity with (transform_c, mesh_c):
  │    │    ├─ model = mat4_from_pos_rot(transform.pos, transform.rot)
  │    │    └─ draw_list_push(&cl->forward_list, mesh.vao, mesh.count, model)
  │    │
  │    ├─ /* skinned meshes (Phase 5) */
  │    │  skinning_pipeline_update(&cl->skin_pipeline, cl->jobs,
  │    │       &cl->ecs.skeletons, &cl->ecs.skins)
  │    │  skinning_pipeline_query_draw_list(&cl->skin_pipeline, &skin_draws)
  │    │
  │    └─ /* debug primitives (Phase 4) */
  │       #ifdef DEBUG_DRAW_ENABLE
  │         for each entity with (transform_c, collider_c):
  │           debug_draw_wire_box(&cl->dd, pos, half, rot, GREEN)
  │         debug_draw_grid(&cl->dd, 1.0f, 50, GRAY)
  │         debug_draw_flush(&cl->dd)   /* upload to GPU */
  │       #endif
  │
  ├─ Stage 9: execute_render_pipeline(cl)
  │    ├─ glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT)
  │    ├─ render_pipeline_execute(&cl->pipeline)
  │    │    ├─ Pass: Skybox
  │    │    │    └─ skybox_pass_submit(view_no_translation, proj)
  │    │    ├─ Pass: Forward
  │    │    │    ├─ bind forward shader (view, proj uniforms)
  │    │    │    ├─ for each draw in cl->forward_list:
  │    │    │    │    ├─ set model uniform
  │    │    │    │    └─ glDrawArrays(GL_TRIANGLES, 0, draw.count)
  │    │    │    ├─ for each draw in skin_draws:
  │    │    │    │    ├─ bind bone palette UBO
  │    │    │    │    └─ glDrawElements(...)
  │    │    │    └─ #ifdef DEBUG_DRAW_ENABLE
  │    │    │         glDrawArrays(GL_LINES, 0, cl->dd.vertex_count)
  │    │    │       #endif
  │    │    └─ Pass: Post
  │    │         └─ post_pass_submit(fbo_texture)
  │    │
  │    └─ #ifdef DEBUG_DRAW_ENABLE
  │         debug_draw_clear(&cl->dd)
  │       #endif
  │
  └─ Stage 10: swap_buffers(cl)
       └─ SDL_GL_SwapWindow(cl->window)
```

---

## Data Flow Diagram

```text
┌──────────────────────────────────────────────────────────────────────────┐
│                          CLIENT PROCESS                                  │
│                                                                          │
│  ┌─────────────┐                                                         │
│  │ RX Thread   │    RUDP packets from server                             │
│  │ (fr_client  │◄──────────────────────────────────── network (UDP)       │
│  │  _rx_t)     │                                                         │
│  │             │──── ACKs ──────────────────────────► network (UDP)       │
│  └──────┬──────┘                                                         │
│         │ fr_client_rx_pop_message()                                     │
│         │ (reliable: stream-reassembled; unreliable: direct)             │
│         ▼                                                                │
│  ┌──────────────────┐                                                    │
│  │ Stage 2: Drain   │                                                    │
│  │ Network Messages │                                                    │
│  └───┬──────────┬───┘                                                    │
│      │          │                                                        │
│      │ reliable │ unreliable                                             │
│      ▼          ▼                                                        │
│  ┌────────┐  ┌────────────┐                                              │
│  │event_  │  │state_buf   │                                              │
│  │queue   │  │(body_state)│                                              │
│  └───┬────┘  └─────┬──────┘                                              │
│      │             │                                                     │
│      ▼             ▼                                                     │
│  ┌────────────┐ ┌──────────────────┐                                     │
│  │ Stage 3:   │ │ Stage 4: Apply   │                                     │
│  │ Process    │ │ State Updates    │                                     │
│  │ Reliable   │ │                  │                                     │
│  │ Events     │ │ pose_interp[i]   │                                     │
│  │            │ │   .push(pos,rot) │                                     │
│  │ spawn →    │ └────────┬─────────┘                                     │
│  │  ecs_create│          │                                               │
│  │ despawn →  │          │                                               │
│  │  ecs_destroy          │                                               │
│  └─────┬──────┘          │                                               │
│        │ ecs_world       │ pose_interpolators[]                          │
│        ▼                 ▼                                               │
│  ┌──────────────────────────────────┐                                    │
│  │        ECS WORLD                 │                                    │
│  │  entity_t[]                      │                                    │
│  │  transform_c[]  ◄── Stage 7:    │                                    │
│  │  mesh_c[]           interpolate  │                                    │
│  │  collider_c[]       poses        │                                    │
│  │  health_c[]                      │                                    │
│  │  status_c[]                      │                                    │
│  └──────────┬───────────────────────┘                                    │
│             │                                                            │
│  ┌──────────┼──────────────────────────────┐                             │
│  │          ▼                              │                             │
│  │  ┌─────────────┐    ┌───────────────┐   │                             │
│  │  │ Stage 1:    │    │ Stage 5:      │   │                             │
│  │  │ Poll SDL    │───►│ Update FPS    │   │                             │
│  │  │ Events      │    │ Controller    │   │                             │
│  │  │ (keys,mouse)│    │ + Camera      │   │                             │
│  │  └─────────────┘    └──────┬────────┘   │                             │
│  │                            │            │                             │
│  │                   view + proj matrices   │                             │
│  │                            │            │                             │
│  │                            ▼            │                             │
│  │                     ┌────────────┐      │                             │
│  │                     │ Stage 6:   │      │                             │
│  │                     │ Send Input │──────┼──────► network (UDP)         │
│  │                     │ to Server  │      │   INPUT_MOVE, INPUT_ROT     │
│  │                     └────────────┘      │                             │
│  └─────────────────────────────────────────┘                             │
│             │                                                            │
│             │ draw lists                                                 │
│             ▼                                                            │
│  ┌──────────────────────────────────────────┐                            │
│  │ Stage 8: Build Render Lists              │                            │
│  │  forward_list[] (static + cube meshes)   │                            │
│  │  skin_draws[]   (skinned meshes)         │                            │
│  │  debug_draw     (wireframes, grid, etc.) │                            │
│  └──────────┬───────────────────────────────┘                            │
│             │                                                            │
│             ▼                                                            │
│  ┌──────────────────────────────────────────┐                            │
│  │ Stage 9: Render Pipeline                 │                            │
│  │  ┌─────────┐  ┌─────────┐  ┌──────┐     │                            │
│  │  │ Skybox  │─►│ Forward │─►│ Post │     │                            │
│  │  └─────────┘  └─────────┘  └──────┘     │                            │
│  └──────────┬───────────────────────────────┘                            │
│             │                                                            │
│             ▼                                                            │
│  ┌──────────────────┐                                                    │
│  │ Stage 10: Swap   │                                                    │
│  │ SDL_GL_SwapWindow│────────────────────────► display                   │
│  └──────────────────┘                                                    │
│                                                                          │
└──────────────────────────────────────────────────────────────────────────┘
```

### Data stores read/written per stage

| Stage | Reads                                    | Writes                                  |
|-------|------------------------------------------|-----------------------------------------|
| 1     | SDL event queue                          | `input_state_t`                         |
| 2     | RX message queues                        | `event_queue`, `state_buf`              |
| 3     | `event_queue`                            | `ecs_world` (create/destroy entities)   |
| 4     | `state_buf`                              | `pose_interpolators[]`                  |
| 5     | `input_state_t`                          | `fps_controller`, `camera` (view/proj)  |
| 6     | `fps_controller`                         | RUDP socket (wire)                      |
| 7     | `pose_interpolators[]`                   | `ecs_world.transform_c[]`              |
| 8     | `ecs_world` (transform, mesh, collider)  | `forward_list`, `skin_draws`, `debug_draw` |
| 9     | draw lists, `camera`, GPU resources      | framebuffer                             |
| 10    | framebuffer                              | display                                 |

### Data ownership

```text
  input_state_t          ──── written by stage 1, read by stage 5
  event_queue            ──── written by stage 2, drained by stage 3
  state_buf              ──── written by stage 2, drained by stage 4
  pose_interpolators[]   ──── written by stage 4, sampled by stage 7
  ecs_world              ──── written by stages 3/7, read by stage 8
  fps_controller / camera──── written by stage 5, read by stages 6/9
  draw lists             ──── written by stage 8, read by stage 9
  debug_draw             ──── written by stage 8, flushed/cleared by stage 9
```

All data is single-threaded on the main thread.  Only the RX thread
(`fr_client_rx_t`) runs concurrently; its output is consumed via
`fr_client_rx_pop_message()` which is the single synchronization point.

---

## Frame Timing

```text
  vsync 0       vsync 1       vsync 2       vsync 3
  │             │             │             │
  ├─ frame 0 ──├─ frame 1 ──├─ frame 2 ──├──
  │             │             │             │
  │ poll        │ poll        │ poll        │
  │ drain net   │ drain net   │ drain net   │
  │ events      │ events      │ events      │
  │ state       │ state       │ state       │
  │ controller  │ controller  │ controller  │
  │ send input  │ send input  │ send input  │
  │ interpolate │ interpolate │ interpolate │
  │ render      │ render      │ render      │
  │ swap        │ swap        │ swap        │
  │             │             │             │

  Interpolation renders at: now - snapshot_interval
  This ensures smooth motion even with jittery packet arrival.
  Extrapolation is clamped to 1.5× interval to limit prediction error.
```

The client runs at display refresh rate (vsync or uncapped).  Network
receive and input send are decoupled from render rate — input is sent
at a capped rate (e.g., 60 Hz) regardless of framerate.

---

## RX Thread Detail

```text
  fr_client_rx_t (separate OS thread):
    loop:
      ├─ net_udp_socket_recvfrom(sock, buf, &len, &addr)
      ├─ net_rudp_peer_receive(peer, buf, len, now_ms)
      │    ├─ if reliable: reassemble into fr_rudp_stream_t
      │    │    └─ fr_rudp_stream_push_frame(stream, data, len)
      │    └─ if unreliable: push to unreliable topic
      ├─ pop completed stream messages → reliable topic
      ├─ net_rudp_peer_tick_resend_via(peer, ...)  /* send ACKs */
      └─ sleep / poll
```

The RX thread is the only concurrency boundary.  It writes to internal
topic channels; the main thread reads from them.  No locks are held
across the boundary — `fr_topic_channel_t` provides lock-free access.
