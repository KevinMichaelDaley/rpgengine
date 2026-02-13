# Client Frame Call Graph & Data Flow (Target)

This document describes the target client frame orchestration and data flow.
See `ref/client_architecture.md` for the narrative design.

### EXISTS vs PLANNED

Call graph uses target concepts (I/O thread, inbox queues). Many building blocks exist
(renderer, stream reassembly, pose interpolation), but the full demux path is a planned integration.

---

## High-Level Call Graph

```text
client_main()
  ├─ start_io_thread(io)
  └─ while (running):
       client_frame(cl)

client_frame(cl)
  ├─ Stage 1: poll_sdl_events()
  │    └─ SDL_PollEvent → update input_state
  │
  ├─ Stage 2: drain_network_inboxes()
  │    ├─ while (inbox_pop(cl->reliable_inbox, msg)):
  │    │    ├─ schema_id = read_u16_le(msg)
  │    │    └─ reliable_event_queue_push(msg)
  │    └─ while (inbox_pop(cl->unreliable_inbox, pkt)):
  │         ├─ schema_id = read_u16_le(pkt)
  │         └─ state_update_queue_push(pkt)
  │
  ├─ Stage 3: process_reliable_events()
  │    └─ while (event_queue_pop(&event)):
  │         ├─ server_event_decode(...)
  │         └─ switch (event.type):
  │              ├─ SPAWN: create ECS entity; set entity.body_id; init pose interp
  │              ├─ DESPAWN: destroy entity; clear body_id cache
  │              └─ ...
  │
  ├─ Stage 4: apply_unreliable_state_updates()
  │    └─ while (state_update_pop(&bs)):
  │         ├─ decode BODY_STATE/STATE_CUBE
  │         ├─ entity = entity_by_body_id[bs.body_id] (derived cache)
  │         ├─ snapshot correction debug: sample interp BEFORE push (old_pos)
  │         ├─ pose_interp_push(entity.pose, now_s, pos, rot, vel, ang_vel, server_time_s)
  │         └─ sample interp AFTER push at same time → corr_raw_pos (correction jump)
  │
  ├─ Stage 5: update_fps_controller_and_camera()
  ├─ Stage 6: enqueue_input_packets_for_io()
  │    └─ io_tx_queue_push([schema_id:u16][payload...])
  │
  ├─ Stage 7: interpolate_poses()
  │    └─ pose_interp_sample(entity.pose, render_time, &pos, &rot)
  │         ├─ t ∈ [0,1]: semi-physical blend (fwd from prev + bwd from curr using vel)
  │         └─ t > 1: hold curr pose (extrapolation disabled)
  ├─ Stage 8: build_render_lists()
  │    ├─ cubes / static meshes
  │    ├─ skinned meshes (later)
  │    └─ debug primitives (wireframe colliders, grid, correction lines)
  ├─ Stage 9: render_pipeline_execute()
  ├─ Stage 10: fr_video_capture_submit_frame(cap) [optional]
  │    ├─ harvest completed PBO fences → map → frame_ring_push
  │    └─ if elapsed ≥ 1/target_fps: begin_readback (glReadPixels → PBO)
  └─ Stage 11: SDL_GL_SwapWindow()

IO thread: io_thread_main()
  ├─ RX:
  │    ├─ recvfrom(udp)
  │    ├─ if packet is RUDP:
  │    │    ├─ net_rudp_peer_receive(peer, ...)
  │    │    └─ if schema==STREAM_FRAME:
  │    │         fr_rudp_stream_push_frame(stream, payload)
  │    │         drain in-order stream messages → reliable_inbox
  │    └─ else:
  │         push raw datagram → unreliable_inbox
  └─ TX:
       drain io_tx_queue → sendto(udp)   (input packets)

Encode thread (optional, spawned by video_capture_create):
  └─ while (!stop):
       ├─ fr_frame_ring_pop() → pixels
       └─ fwrite(pixels) → ffmpeg pipe (H.264)
```

---

## Data Flow Diagram (DFD)

```text
┌──────────────────────────────────────────────────────────────────────────┐
│                          CLIENT PROCESS                                  │
│                                                                          │
│  ┌──────────────────────────────┐      reliable_inbox     ┌────────────┐ │
│  │ I/O thread (sockets + demux) │────────────────────────►│ Main thread│ │
│  │  RX: recvfrom(udp)           │      unreliable_inbox   │ (SDL + GL) │ │
│  │   ├─ RUDP → stream reasm     │────────────────────────►│            │ │
│  │   └─ raw → state datagrams   │                         │            │ │
│  │  TX: sendto(udp)             │◄──── io_tx_queue ───────│ input enc  │ │
│  └──────────────────────────────┘                         └────────────┘ │
│                                                                          │
└──────────────────────────────────────────────────────────────────────────┘
```

---

## Message Layouts

Reliable inbox message:
```
[schema_id:u16][payload...]
```

Unreliable inbox datagram:
```
[schema_id:u16][payload...]
```

---

## Failure Modes / Pitfalls

- State may arrive before spawn; drop/buffer briefly per body_id.
- Keep protocol demux explicit to avoid routing raw UDP into RUDP.
- Reliable stream must be sized for spawn bursts (batching + window).
