# Networking Call Graph and Execution Flow

This document provides a complete staged ASCII diagram of the call graph and
execution flow for the Ferrum networking subsystem. It shows:
- The server-side demux pump → per-client fiber → topic publish path
- The client-side RX thread → reassembly → topic delivery path
- The replication encode/decode pipeline
- Full function signatures, data flows, and ownership semantics

---

## Legend

```
┌─────────────────────────────────────────────────────────────────────────────┐
│ LEGEND                                                                      │
├─────────────────────────────────────────────────────────────────────────────┤
│ ═══════  Top-level entry point / system boundary                            │
│ ───────  Synchronous call (caller waits for return)                         │
│ ·······  Job dispatch (async, returns immediately, syncs on counter)        │
│ ┄┄┄┄┄┄┄  Data flow (output becomes input to next stage)                     │
│                                                                             │
│ [SYNC]       Single-threaded synchronous execution                          │
│ [THREADED]   Runs on its own OS thread                                      │
│ [FIBER]      Runs on a job-system fiber                                     │
│ [LOCK-FREE]  Lock-free atomic operations                                    │
│ [LOCKED]     Short mutex-guarded critical section                           │
│                                                                             │
│ ──▶       Function call                                                     │
│ ══▶       Data dependency (output flows to next stage input)                │
│ ◆         Sync point (thread join / counter wait)                           │
│ ●         Backpressure boundary (ring buffer full → drop/fail)              │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## Data Structures Overview

```
═══════════════════════════════════════════════════════════════════════════════
WIRE PROTOCOL STRUCTURES
═══════════════════════════════════════════════════════════════════════════════

net_packet_header_t                  12-byte packet header (network order)
├── protocol_id: uint32_t            Magic number (e.g., 0x52555038 = 'RUP8')
├── sequence: uint16_t               Sender sequence number
├── ack: uint16_t                    Highest received remote sequence
└── ack_bits: uint32_t               Bitfield of 32 prior ACKs

net_rudp_wire_frame_view_t           8-byte frame header + payload view
├── flags: uint8_t                   FLAG_RELIABLE (0x01), FLAG_FRAGMENT (0x02)
├── schema_id: uint16_t             Replication schema identifier
├── payload: const uint8_t*          Points into caller's packet buffer
└── payload_size: size_t             Payload byte count

Wire Layout (UDP datagram):
┌──────────────┬──────────────────┬─────────────────────┐
│ packet_header │ frame_header     │ payload             │
│ (12 bytes)    │ (8 bytes)        │ (0..~492 bytes)     │
└──────────────┴──────────────────┴─────────────────────┘
  Total: ≤ 512 bytes (NET_RUDP_MAX_PACKET_SIZE)

═══════════════════════════════════════════════════════════════════════════════
RELIABILITY STRUCTURES (per-peer)
═══════════════════════════════════════════════════════════════════════════════

net_ack_window_t                     Sliding ACK window
├── ack: uint16_t                    Highest received sequence
├── ack_bits: uint32_t               33-deep duplicate/ordering bitfield
└── initialized: uint8_t             Guard for first-packet bootstrap

net_rudp_send_slot_t                 One reliable send slot
├── sequence: uint16_t               Assigned sequence number
├── last_send_ms: uint64_t           Timestamp of last (re)send
├── size: uint16_t                   Packet size
├── used: uint8_t                    Slot occupancy flag
└── packet_bytes[512]: uint8_t       Full encoded packet (for resend)

net_rudp_peer_t                      Per-peer RUDP state
├── protocol_id: uint32_t            Expected magic
├── next_sequence: uint16_t          Next outbound sequence
├── recv_window: net_ack_window_t    Inbound ACK tracking
├── resend_interval_ms: uint32_t     Retransmission timeout
├── max_slot_age_ms: uint32_t        TTL for unACKed slots (0 = no expiry)
├── send_slots: net_rudp_send_slot_t*  Reliable send ring buffer
├── send_slot_count: size_t          Number of send slots
├── frag_enabled: uint8_t            Fragmentation/reassembly toggle
├── reasm_buf: uint8_t*              Reassembly scratch buffer
├── reasm_buf_cap: size_t            Reassembly buffer capacity
└── reasm_storage[4096]: uint8_t     Default reassembly backing

═══════════════════════════════════════════════════════════════════════════════
STREAM REASSEMBLY STRUCTURES
═══════════════════════════════════════════════════════════════════════════════

net_reliable_channel_t               Per-channel ordered delivery buffer
├── payloads: uint8_t*               Contiguous payload storage
├── sizes: size_t*                   Per-slot payload sizes
├── sequences: uint16_t*             Per-slot sequence numbers
├── occupied: uint8_t*               Per-slot occupancy flags
├── slot_count: size_t               Buffer depth
├── max_payload_size: size_t         Maximum payload per slot
├── next_send_sequence: uint16_t     Next outbound sequence
├── next_receive_sequence: uint16_t  Next expected inbound sequence
└── initialized: uint8_t             Init guard

fr_rudp_stream_t (opaque)            Multi-channel stream reassembly context
├── channels: net_reliable_channel_t[]  Per-channel reliable buffers
├── config: fr_rudp_stream_config_t     Creation parameters
└── topics: fr_topic_channel_t**        Optional topic pump targets

═══════════════════════════════════════════════════════════════════════════════
TOPIC / MESSAGE DISPATCH STRUCTURES
═══════════════════════════════════════════════════════════════════════════════

fr_topic_channel_t (opaque)          Ring buffer for decoded messages
├── ring: uint8_t*                   Byte-level ring storage
├── capacity: uint32_t               Max messages
├── capacity_bytes: uint32_t         Ring byte capacity
├── max_message_size: uint32_t       Per-message size limit
├── head, tail: uint32_t             Ring cursors
├── lock: mtx_t                      Short critical section guard
├── backpressure: uint32_t           FAIL / DROP_OLDEST / DROP_NEWEST
└── stat_dropped: uint64_t           Dropped message counter

fr_topic_dispatcher_t (opaque)       Background pump → handler dispatch
├── sys: job_system_t*               Job system for handler fiber dispatch
├── topics: fr_topic_channel_t**     Polled topic channels
├── num_topics: uint32_t             Number of channels
├── handlers[]: { fn, user, priority, preferred_worker }
└── pump_thread: thrd_t              Background poll thread

═══════════════════════════════════════════════════════════════════════════════
SERVER RUNTIME STRUCTURES
═══════════════════════════════════════════════════════════════════════════════

fr_server_client_fiber_t (opaque)    Per-client fiber state
├── stream: fr_rudp_stream_t*        Owned reassembly context
├── topics: fr_topic_channel_t**     Topic publish targets
└── config: fr_server_client_fiber_config_t

fr_server_net_runtime_t (opaque)     Server demux + client fiber manager
├── config: fr_server_net_runtime_config_t
├── jobs: job_system_t*              Fiber scheduler (owned or borrowed)
├── socket: net_udp_socket_t*        UDP socket (or callback-based)
├── inbound_topic: fr_topic_channel_t*  Global inbound decoded messages
├── client_fibers[max_clients]: fr_server_client_fiber_t*
├── out_reliable[max_clients]: fr_topic_channel_t*   Per-client outbound reliable
└── out_unreliable[max_clients]: fr_topic_channel_t* Per-client outbound unreliable

fr_state_update_queue_t (opaque)     MPMC decoded message queue
├── capacity: uint32_t               Max queued updates
├── max_payload_size: uint32_t       Per-update payload limit
├── entries[]: { client_id, schema_id, payload[], payload_size }
└── lock: mtx_t                      Short critical section

═══════════════════════════════════════════════════════════════════════════════
CLIENT RUNTIME STRUCTURES
═══════════════════════════════════════════════════════════════════════════════

fr_client_rx_t (opaque)              Client receive runtime
├── socket: net_udp_socket_t*        UDP socket (owned or borrowed)
├── channels[max_channels]: net_reliable_channel_t   Reassembly buffers
├── topics: fr_topic_channel_t**     Optional topic push targets
├── rx_thread: thrd_t                Background receive thread
├── running: atomic_bool             Thread lifecycle flag
└── recv_cb: ssize_t(*)(...)         Optional test callback override

═══════════════════════════════════════════════════════════════════════════════
REPLICATION SCHEMA STRUCTURES
═══════════════════════════════════════════════════════════════════════════════

net_schema_registry_t                Fixed-size schema → payload_size map
├── schema_ids[256]: uint16_t        Registered schema IDs
├── payload_sizes[256]: uint16_t     Expected payload sizes
└── count: size_t                    Number registered

Replication Messages:
├── net_repl_spawn_t         (20 bytes)  entity_id, owner_client_id, join_time, pos_mm
├── net_repl_state_cube_t    (40 bytes)  server_tick, entity_id, pos_mm, rot_snorm16, omega
├── net_repl_join_t          (schema 0x2001)
├── net_repl_welcome_t       (schema 0x2004)
├── net_repl_spawn_batch_t   (schema 0x2005)  multiple spawns per message
├── net_repl_state_cube_batch_t (schema 0x2006) multiple state cubes per message
└── net_repl_input_rot_t     (schema 0x2007)  client → server rotation input

Quantization:
├── net_qvec3_mm_t           int32 × 3, millimeter resolution
└── net_qquat_snorm16_t      int16 × 4, snorm16 components (w >= 0 canonical)
```

---

## Complete Call Graph

```
═══════════════════════════════════════════════════════════════════════════════
              SERVER-SIDE: DEMUX PUMP → CLIENT FIBER → TOPIC PUBLISH
═══════════════════════════════════════════════════════════════════════════════

┌──────────────────────────────────────────────────────────────────────────────
│ STAGE S0: SERVER RUNTIME CREATION [SYNC]
├──────────────────────────────────────────────────────────────────────────────
│
├──▶ fr_server_net_runtime_create(&cfg)
│    │
│    │   fr_server_net_runtime_t *fr_server_net_runtime_create(
│    │       const fr_server_net_runtime_config_t *cfg   [IN]  runtime config
│    │   )
│    │
│    │   ALLOCATES:
│    │   ├── Runtime struct (heap)
│    │   ├── Per-client fiber slots: cfg->max_clients × sizeof(ptr)
│    │   ├── Per-client outbound reliable topics: max_clients × fr_topic_channel_create()
│    │   ├── Per-client outbound unreliable topics: max_clients × fr_topic_channel_create()
│    │   └── Internal job system (if cfg->jobs == NULL)
│    │
│    │   WIRES:
│    │   ├── rt->inbound_topic = cfg->inbound_topic   (borrowed, not owned)
│    │   ├── rt->socket = cfg->socket                 (borrowed)
│    │   └── rt->jobs = cfg->jobs or internal          (owned if internal)
│    │
│    └── OUTPUT ═══════════════════════════════════════════════════════════════▶
│                fr_server_net_runtime_t* (opaque runtime handle)
│
├──────────────────────────────────────────────────────────────────────────────
│ STAGE S1: DEMUX PUMP [SYNC, called from server main loop] — ~5-50 µs/call
├──────────────────────────────────────────────────────────────────────────────
│
├──▶ fr_server_net_runtime_pump(rt, now_ms)
│    │
│    │   bool fr_server_net_runtime_pump(
│    │       fr_server_net_runtime_t *rt,   [IN/OUT]  runtime state
│    │       uint64_t now_ms                [IN]      monotonic timestamp
│    │   )
│    │
│    │   ALGORITHM:
│    │   ├── LOOP (nonblocking, drain all pending datagrams):
│    │   │   │
│    │   │   ├──▶ net_udp_socket_recvfrom(rt->socket, &from, buf, cap, &size)
│    │   │   │    │   OR rt->config.recvfrom_cb(rt->io_user, &from, buf, cap, &size)
│    │   │   │    │
│    │   │   │    │   RETURNS: NET_UDP_SOCKET_OK on data, _EMPTY when drained
│    │   │   │    │
│    │   │   │    └── OUTPUT: raw UDP datagram bytes + source address
│    │   │   │
│    │   │   ├──▶ net_rudp_wire_decode(&header, &frame, buf, size)
│    │   │   │    │
│    │   │   │    │   int net_rudp_wire_decode(
│    │   │   │    │       net_packet_header_t *out_header,           [OUT]
│    │   │   │    │       net_rudp_wire_frame_view_t *out_frame,     [OUT]
│    │   │   │    │       const uint8_t *packet,                     [IN]
│    │   │   │    │       size_t packet_size                         [IN]
│    │   │   │    │   )
│    │   │   │    │
│    │   │   │    │   VALIDATES:
│    │   │   │    │   ├── Packet >= 20 bytes (header 12 + frame header 8)
│    │   │   │    │   ├── Protocol ID matches
│    │   │   │    │   └── Payload size within bounds
│    │   │   │    │
│    │   │   │    └── OUTPUT: decoded header + frame view (payload points into buf)
│    │   │   │
│    │   │   ├── Route to client fiber by source address:
│    │   │   │   ├── Lookup existing client_id by addr (or allocate new slot)
│    │   │   │   └── client_fiber = rt->client_fibers[client_id]
│    │   │   │
│    │   │   └──▶ fr_server_client_fiber_inject_frame(client_fiber, frame_data, frame_len)
│    │   │        │
│    │   │        │   bool fr_server_client_fiber_inject_frame(
│    │   │        │       fr_server_client_fiber_t *fiber,   [IN/OUT]
│    │   │        │       const unsigned char *frame,        [IN]  frame bytes
│    │   │        │       size_t len                         [IN]  frame length
│    │   │        │   )
│    │   │        │
│    │   │        │   CALLS:
│    │   │        │   ├──▶ fr_rudp_stream_push_frame(fiber->stream, frame, len)
│    │   │        │   │    │
│    │   │        │   │    │   Frame format: [seq:u16 LE][chan:u16 LE][payload]
│    │   │        │   │    │
│    │   │        │   │    │   CALLS:
│    │   │        │   │    │   └──▶ net_reliable_channel_send_sequence(
│    │   │        │   │    │            &stream->channels[chan_id], seq, payload, size)
│    │   │        │   │    │        │
│    │   │        │   │    │        │   ALGORITHM:
│    │   │        │   │    │        │   ├── Map seq to slot index (seq % slot_count)
│    │   │        │   │    │        │   ├── Copy payload into slot
│    │   │        │   │    │        │   ├── Mark slot occupied
│    │   │        │   │    │        │   └── Increment count
│    │   │        │   │    │        │
│    │   │        │   │    │        └── EFFECT: Frame buffered for in-order delivery
│    │   │        │   │    │
│    │   │        │   │    │   THEN (if topics configured):
│    │   │        │   │    │   └── Pump in-order messages to topic channels:
│    │   │        │   │    │       └──▶ net_reliable_channel_receive(chan, out, cap, &size)
│    │   │        │   │    │            │   RETURNS: OK if next_receive_sequence is available
│    │   │        │   │    │            └──▶ fr_topic_channel_push(topic, out, size)
│    │   │        │   │    │                 │   EFFECT: Message appended to ring buffer
│    │   │        │   │    │                 └── ● Backpressure: DROP_OLDEST / DROP_NEWEST / FAIL
│    │   │        │   │    │
│    │   │        │   │    └── RETURNS: true on accept, false on duplicate/full
│    │   │        │   │
│    │   │        │   └── RETURNS: true on success
│    │   │        │
│    │   │        └── OUTPUT: Frame buffered in client fiber's stream → topic
│    │   │
│    │   └── BREAK when recvfrom returns EMPTY
│    │
│    └── RETURNS: true (false on invalid args)
│
├──────────────────────────────────────────────────────────────────────────────
│ STAGE S2: CLIENT FIBER EXECUTION [FIBER, job-system scheduled]
├──────────────────────────────────────────────────────────────────────────────
│
├──▶ fr_server_net_runtime_run_fibers(rt, max_spins)
│    │
│    │   bool fr_server_net_runtime_run_fibers(
│    │       fr_server_net_runtime_t *rt,   [IN/OUT]
│    │       uint32_t max_spins             [IN]  bounded iteration count
│    │   )
│    │
│    │   ALGORITHM:
│    │   ├── For each active client fiber:
│    │   │   └──▶ job_dispatch(rt->jobs, client_fiber_job_fn, fiber_ctx, priority, &counter)
│    │   │        │
│    │   │        │   client_fiber_job_fn(ctx):
│    │   │        │   ├── Pop decoded messages from fiber's stream channels
│    │   │        │   ├── Publish to rt->inbound_topic via fr_topic_channel_push()
│    │   │        │   │   │   Message format: [client_id:u16][schema_id:u16]
│    │   │        │   │   │                   [flags:u8][reserved:u8][payload...]
│    │   │        │   │   └── ● Backpressure on inbound topic
│    │   │        │   │
│    │   │        │   ├── Poll per-client outbound topics:
│    │   │        │   │   ├──▶ fr_topic_channel_pop(rt->out_reliable[client_id], &msg, &len)
│    │   │        │   │   │    └── Package and send via RUDP reliable path
│    │   │        │   │   └──▶ fr_topic_channel_pop(rt->out_unreliable[client_id], &msg, &len)
│    │   │        │   │        └── Package and send via RUDP unreliable path
│    │   │        │   │
│    │   │        │   └── Tick RUDP resend for this client's peer:
│    │   │        │       └──▶ net_rudp_peer_tick_resend_via(peer, io_user, sendto_cb, &to, now_ms)
│    │   │        │            │
│    │   │        │            │   ALGORITHM:
│    │   │        │            │   ├── For each occupied send_slot:
│    │   │        │            │   │   ├── IF age > max_slot_age_ms (default 5s):
│    │   │        │            │   │   │   └── Expire slot (slot->used = 0), skip
│    │   │        │            │   │   ├── IF now_ms - slot->last_send_ms > resend_interval_ms:
│    │   │        │            │   │   │   ├── sendto_cb(io_user, &to, slot->packet_bytes, slot->size)
│    │   │        │            │   │   │   └── slot->last_send_ms = now_ms
│    │   │        │            │   │   └── ELSE: skip
│    │   │        │            │   └── RETURNS: count of resent packets
│    │   │        │            │
│    │   │        │            └── EFFECT: Unacknowledged reliable packets retransmitted or expired
│    │   │        │
│    │   │        └── Fiber completes → job_counter_dec(&counter)
│    │   │
│    │   └── ◆ job_system_wait_idle(rt->jobs) or bounded spin
│    │
│    └── RETURNS: true on success

═══════════════════════════════════════════════════════════════════════════════
              SERVER-SIDE: OUTBOUND TX PATH (reliable + unreliable)
═══════════════════════════════════════════════════════════════════════════════

┌──────────────────────────────────────────────────────────────────────────────
│ STAGE S3: OUTBOUND MESSAGE ENQUEUE [SYNC/LOCKED] — ~0.5 µs/msg
├──────────────────────────────────────────────────────────────────────────────
│
│   Simulation/gameplay jobs produce outbound messages by pushing to
│   per-client outbound topics:
│
├──▶ fr_server_net_runtime_client_out_topics(rt, client_id, &out_rel, &out_unrel)
│    │
│    │   bool fr_server_net_runtime_client_out_topics(
│    │       fr_server_net_runtime_t *rt,          [IN]
│    │       uint16_t client_id,                   [IN]
│    │       fr_topic_channel_t **out_reliable,    [OUT]
│    │       fr_topic_channel_t **out_unreliable   [OUT]
│    │   )
│    │
│    └── OUTPUT: per-client topic channel handles
│
├──▶ fr_topic_channel_push(out_reliable, encoded_msg, msg_len)
│    │   Push reliable replication message (spawn, welcome, etc.)
│    └── ● Backpressure applies
│
├──▶ fr_topic_channel_push(out_unreliable, encoded_msg, msg_len)
│    │   Push unreliable state update (state_cube, etc.)
│    └── ● Backpressure applies
│
├──────────────────────────────────────────────────────────────────────────────
│ STAGE S4: OUTBOUND WIRE ENCODE + TRANSMIT [FIBER] — ~2-10 µs/packet
├──────────────────────────────────────────────────────────────────────────────
│
│   Per-client fiber polls outbound topics and transmits:
│
├──▶ net_rudp_peer_send_reliable_via(peer, io_user, sendto_cb, &to, now_ms,
│    │                                schema_id, payload, payload_size, &out_seq)
│    │
│    │   ALGORITHM:
│    │   ├── Find free send slot
│    │   ├── Assign sequence = peer->next_sequence++
│    │   ├── Build packet header: { protocol_id, sequence, ack, ack_bits }
│    │   ├──▶ net_rudp_wire_encode(&header, FLAG_RELIABLE, schema_id,
│    │   │                          payload, payload_size, buf, cap, &size)
│    │   ├── Copy encoded packet into send_slot (for retransmit)
│    │   ├── sendto_cb(io_user, &to, buf, size)
│    │   └── slot->last_send_ms = now_ms
│    │
│    └── EFFECT: Reliable packet sent and buffered for retransmit
│
├──▶ net_rudp_peer_send_unreliable_via(peer, io_user, sendto_cb, &to, now_ms,
│    │                                  schema_id, payload, payload_size)
│    │
│    │   ALGORITHM:
│    │   ├── Build packet header: { protocol_id, peer->next_sequence++, ack, ack_bits }
│    │   ├──▶ net_rudp_wire_encode(&header, 0, schema_id, payload, size, buf, cap, &out)
│    │   └── sendto_cb(io_user, &to, buf, out)
│    │
│    └── EFFECT: Unreliable packet sent (no buffering, no retransmit)

═══════════════════════════════════════════════════════════════════════════════
              SERVER-SIDE: INBOUND PROCESSING (topic → simulation)
═══════════════════════════════════════════════════════════════════════════════

┌──────────────────────────────────────────────────────────────────────────────
│ STAGE S5: TOPIC DISPATCH → SIMULATION JOBS [THREADED/FIBER]
├──────────────────────────────────────────────────────────────────────────────
│
├──▶ fr_topic_dispatcher_create(sys, topics, num_topics)
│    │   Create dispatcher bound to job system + topic channels
│    └── OUTPUT: fr_topic_dispatcher_t*
│
├──▶ fr_topic_dispatcher_register(disp, topic_index, on_message, user, priority, worker)
│    │
│    │   int fr_topic_dispatcher_register(
│    │       fr_topic_dispatcher_t *disp,             [IN/OUT]
│    │       uint32_t topic_index,                    [IN]
│    │       void (*on_message)(const uint8_t*, size_t, void*),  [IN]  handler
│    │       void *user,                              [IN]  user context
│    │       int priority,                            [IN]  job priority hint
│    │       uint32_t preferred_worker                [IN]  worker affinity hint
│    │   )
│    │
│    └── EFFECT: Handler registered for topic index
│
├──▶ fr_topic_dispatcher_start(disp)
│    │
│    │   ALGORITHM:
│    │   ├── Spawns pump thread:
│    │   │   └── LOOP:
│    │   │       ├── For each registered topic:
│    │   │       │   ├──▶ fr_topic_channel_pop(topic, buf, &len)
│    │   │       │   └── IF message available:
│    │   │       │       └──▶ job_dispatch_to(sys, handler_wrapper, {msg, handler},
│    │   │       │                            priority, NULL, preferred_worker)
│    │   │       │            │
│    │   │       │            │   handler_wrapper:
│    │   │       │            │   └── handler->on_message(msg_data, msg_len, handler->user)
│    │   │       │            │
│    │   │       │            └── EFFECT: Handler runs on preferred worker fiber
│    │   │       │
│    │   │       └── Sleep / yield if no messages
│    │   │
│    │   └── RETURNS: 0 on success
│    │
│    └── EFFECT: Background pump thread running
│
│   ALTERNATIVELY (without dispatcher):
│
├──▶ fr_state_update_queue_pop(q, &client_id, &schema_id, payload, &size)
│    │
│    │   Simulation jobs poll the global update queue directly:
│    │
│    │   bool fr_state_update_queue_pop(
│    │       fr_state_update_queue_t *q,      [IN/OUT]
│    │       uint16_t *out_client_id,         [OUT]
│    │       uint16_t *out_schema_id,         [OUT]
│    │       uint8_t *out_payload,            [OUT]
│    │       uint16_t *inout_payload_size     [IN/OUT]
│    │   )
│    │
│    └── RETURNS: true if update dequeued, false if empty

═══════════════════════════════════════════════════════════════════════════════
              CLIENT-SIDE: RX THREAD → REASSEMBLY → TOPIC DELIVERY
═══════════════════════════════════════════════════════════════════════════════

┌──────────────────────────────────────────────────────────────────────────────
│ STAGE C0: CLIENT RX RUNTIME CREATION [SYNC]
├──────────────────────────────────────────────────────────────────────────────
│
├──▶ fr_client_rx_create(&cfg)
│    │
│    │   fr_client_rx_t *fr_client_rx_create(
│    │       const fr_client_rx_config_t *cfg   [IN]  client RX config
│    │   )
│    │
│    │   ALLOCATES:
│    │   ├── RX context struct (heap)
│    │   ├── Per-channel reliable buffers: max_channels × net_reliable_channel_init()
│    │   └── Wires topics[] for decoded message push
│    │
│    └── OUTPUT ═══════════════════════════════════════════════════════════════▶
│                fr_client_rx_t* (opaque RX context)
│
├──────────────────────────────────────────────────────────────────────────────
│ STAGE C1: CLIENT RX THREAD [THREADED, background] — continuous
├──────────────────────────────────────────────────────────────────────────────
│
├──▶ fr_client_rx_start(rx)
│    │
│    │   bool fr_client_rx_start(fr_client_rx_t *rx)
│    │
│    │   SPAWNS: rx_thread_fn(rx)
│    │   │
│    │   └── LOOP (while rx->running):
│    │       │
│    │       ├──▶ recv_cb(rx->recv_user, buf, cap)
│    │       │    OR net_udp_socket_recvfrom(rx->socket, &from, buf, cap, &size)
│    │       │    │
│    │       │    └── OUTPUT: raw UDP datagram
│    │       │
│    │       ├── Parse frame header: [seq:u16 LE][chan_id:u16 LE][payload...]
│    │       │
│    │       ├──▶ net_reliable_channel_send_sequence(
│    │       │        &rx->channels[chan_id], seq, payload, payload_size)
│    │       │    │
│    │       │    └── EFFECT: Frame buffered in channel's slot ring
│    │       │
│    │       ├── Pump in-order delivery:
│    │       │   └── WHILE net_reliable_channel_receive(&rx->channels[chan_id], out, cap, &size)
│    │       │              == NET_RELIABLE_OK:
│    │       │       │
│    │       │       └──▶ fr_topic_channel_push(rx->topics[chan_id], out, size)
│    │       │            │
│    │       │            └── EFFECT: Decoded message available to gameplay/render
│    │       │
│    │       └── Continue loop
│    │
│    └── RETURNS: true on thread creation success
│
├──────────────────────────────────────────────────────────────────────────────
│ STAGE C2: CLIENT MESSAGE CONSUMPTION [SYNC, from gameplay/render thread]
├──────────────────────────────────────────────────────────────────────────────
│
├──▶ fr_client_rx_pop_message(rx, channel_id, out, &inout_len)
│    │
│    │   Direct channel access (bypasses topic dispatch):
│    │
│    │   bool fr_client_rx_pop_message(
│    │       fr_client_rx_t *rx,       [IN/OUT]
│    │       uint32_t channel_id,      [IN]
│    │       uint8_t *out,             [OUT]  message bytes
│    │       size_t *inout_len         [IN/OUT]  capacity → actual size
│    │   )
│    │
│    └── RETURNS: true if message popped, false if empty
│
├── OR via topic channel:
│
├──▶ fr_topic_channel_pop(topic, out, &inout_len)
│    │
│    │   Gameplay/render jobs poll topic channels populated by RX thread
│    │
│    └── RETURNS: true if message available

═══════════════════════════════════════════════════════════════════════════════
              CLIENT-SIDE: TX PATH (outbound to server)
═══════════════════════════════════════════════════════════════════════════════

┌──────────────────────────────────────────────────────────────────────────────
│ STAGE C3: CLIENT TX (per-frame, from gameplay thread) — ~2-10 µs
├──────────────────────────────────────────────────────────────────────────────
│
├──▶ net_rudp_peer_send_reliable(peer, sock, &server_addr, now_ms,
│    │                            schema_id, payload, size, &out_seq)
│    │
│    │   Client sends reliable messages (e.g., input_spawn, join):
│    │   ├── Encode packet with ACK piggyback
│    │   ├── Buffer in send slot for retransmit
│    │   └── net_udp_socket_sendto(sock, &server_addr, packet, packet_size)
│    │
│    └── EFFECT: Reliable input sent to server
│
├──▶ net_rudp_peer_send_unreliable(peer, sock, &server_addr, now_ms,
│    │                              schema_id, payload, size)
│    │
│    │   Client sends unreliable messages (e.g., input_move, keepalive):
│    │   ├── Encode packet with ACK piggyback (no send slot buffering)
│    │   └── net_udp_socket_sendto(sock, &server_addr, packet, packet_size)
│    │
│    └── EFFECT: Unreliable input sent to server
│
├──▶ net_rudp_peer_tick_resend(peer, sock, &server_addr, now_ms)
│    │
│    │   Retransmit unacknowledged reliable packets past interval;
│    │   expire slots older than max_slot_age_ms (default 5s):
│    │   └── For each occupied send_slot: expire if too old, else resend
│    │
│    └── EFFECT: Reliable retransmission + slot expiry

═══════════════════════════════════════════════════════════════════════════════
              REPLICATION ENCODE/DECODE PIPELINE
═══════════════════════════════════════════════════════════════════════════════

┌──────────────────────────────────────────────────────────────────────────────
│ STAGE R1: SCHEMA REGISTRY DECODE [SYNC] — ~0.2 µs
├──────────────────────────────────────────────────────────────────────────────
│
├──▶ net_schema_registry_decode_packet(registry, bytes, size,
│    │                                  &out_schema_id, &out_payload, &out_size)
│    │
│    │   ALGORITHM:
│    │   ├── Read schema_id (u16 BE) from bytes[0..1]
│    │   ├── Read payload_size (u16 BE) from bytes[2..3]
│    │   ├── Validate schema_id is registered
│    │   ├── Validate payload_size matches registered expected size
│    │   └── Return pointer into bytes[] for payload
│    │
│    └── OUTPUT: schema_id + payload view into original buffer
│
├──────────────────────────────────────────────────────────────────────────────
│ STAGE R2: REPLICATION MESSAGE DECODE [SYNC] — ~0.1-0.3 µs
├──────────────────────────────────────────────────────────────────────────────
│
├──▶ net_repl_spawn_decode(&msg, payload, payload_size)
│    │   Decode SPAWN (20 bytes): entity_id, owner_client_id, join_time, pos_mm
│    │   ├──▶ Internal: reads fields as LE u32/u16/i32
│    │   └── OUTPUT: net_repl_spawn_t populated
│
├──▶ net_repl_state_cube_decode(&msg, payload, payload_size)
│    │   Decode STATE_CUBE (40 bytes): server_tick, entity_id, pos_mm,
│    │       rot_snorm16, input_event_id, omega_axis/speed
│    │   ├──▶ Internal: reads fields as LE
│    │   └── OUTPUT: net_repl_state_cube_t populated
│
├──────────────────────────────────────────────────────────────────────────────
│ STAGE R3: QUANTIZATION (shared by encode + decode) [SYNC] — ~0.05 µs
├──────────────────────────────────────────────────────────────────────────────
│
├──▶ net_quantize_vec3_mm(v, &out)
│    │   float meters → int32 millimeters (deterministic rounding, halves away from zero)
│    │   Range: ±2,147,483 meters
│    └── OUTPUT: net_qvec3_mm_t { x_mm, y_mm, z_mm }
│
├──▶ net_dequantize_vec3_mm(q, &out)
│    │   int32 millimeters → float meters
│    └── OUTPUT: vec3_t
│
├──▶ net_quantize_quat_snorm16(q, &out)
│    │   float quaternion → int16 snorm16 (canonical: w >= 0)
│    └── OUTPUT: net_qquat_snorm16_t { x, y, z, w }
│
├──▶ net_dequantize_quat_snorm16(q, &out)
│    │   int16 snorm16 → float quaternion (normalized, w >= 0)
│    └── OUTPUT: quat_t
│
├──▶ net_anim_time_u16_add_wrap(t, delta)
│    │   Wrapping u16 time addition for animation sync
│    └── OUTPUT: uint16_t (wrapped)
│
├──▶ net_anim_time_u16_delta_signed(a, b)
│    │   Shortest-path signed delta in u16 time domain
│    └── OUTPUT: int (signed delta)
│
├──────────────────────────────────────────────────────────────────────────────
│ STAGE R4: REPLICATION MESSAGE ENCODE [SYNC] — ~0.1-0.3 µs
├──────────────────────────────────────────────────────────────────────────────
│
├──▶ net_repl_spawn_encode(&msg, out_payload, out_size)
│    │   Encode SPAWN: entity_id, owner_client_id, join_time_u16, pos_mm → 20 bytes LE
│    └── OUTPUT: encoded payload bytes
│
├──▶ net_repl_state_cube_encode(&msg, out_payload, out_size)
│    │   Encode STATE_CUBE: server_tick, entity_id, pos_mm, rot_snorm16,
│    │       input_event_id, omega_axis/speed → 40 bytes LE
│    └── OUTPUT: encoded payload bytes

═══════════════════════════════════════════════════════════════════════════════
              ACK WINDOW MECHANICS (shared by client + server)
═══════════════════════════════════════════════════════════════════════════════

┌──────────────────────────────────────────────────────────────────────────────
│ ACK TRACKING [SYNC, per peer_receive call] — ~0.1 µs
├──────────────────────────────────────────────────────────────────────────────
│
├──▶ net_ack_window_receive(&window, sequence)
│    │
│    │   ALGORITHM:
│    │   ├── IF sequence > window->ack (newer):
│    │   │   ├── Shift ack_bits left by (sequence - ack)
│    │   │   ├── Set bit 0 for the old ack position
│    │   │   └── window->ack = sequence
│    │   ├── ELIF sequence == ack - delta (within 32-bit window):
│    │   │   ├── Set corresponding bit in ack_bits
│    │   │   └── RETURN: DUPLICATE if already set
│    │   └── ELSE:
│    │       └── RETURN: OUT_OF_WINDOW (too old)
│    │
│    └── RETURNS: OK / DUPLICATE / OUT_OF_WINDOW
│
│   On send, piggyback ACK state in every outgoing packet:
│   ├── header.ack = net_ack_window_ack(&recv_window)
│   └── header.ack_bits = net_ack_window_ack_bits(&recv_window)
│
│   On receive, retire acknowledged send slots:
│   ├── IF header.ack matches a send_slot.sequence: mark slot unused
│   └── For each set bit in header.ack_bits: retire matching send_slot
```

---

## End-to-End Data Flow Summary

```
═══════════════════════════════════════════════════════════════════════════════
CLIENT → SERVER (input path)
═══════════════════════════════════════════════════════════════════════════════

 Client Gameplay         Client TX             Network           Server Demux
 ┌─────────────┐   ┌──────────────────┐   ┌───────────┐   ┌────────────────┐
 │ input_rot   │──▶│ quantize         │──▶│ UDP send  │──▶│ recvfrom       │
 │ join        │   │ encode           │   │           │   │ wire_decode    │
 │ commands    │   │ rudp_send_*      │   │           │   │ route → fiber  │
 └─────────────┘   └──────────────────┘   └───────────┘   └───────┬────────┘
                                                                   │
                   ┌──────────────────┐   ┌───────────┐            │
                   │ Simulation Jobs  │◀──│ topic_pop │◀───────────┘
                   │ (apply input)    │   │ or queue  │   inject_frame
                   └──────────────────┘   └───────────┘   → stream → topic

═══════════════════════════════════════════════════════════════════════════════
SERVER → CLIENT (replication path)
═══════════════════════════════════════════════════════════════════════════════

 Server Simulation       Server TX             Network           Client RX
 ┌─────────────┐   ┌──────────────────┐   ┌───────────┐   ┌────────────────┐
 │ spawn       │──▶│ topic_push       │──▶│ fiber TX  │──▶│ rx_thread      │
 │ state_cube  │   │ (per-client out) │   │ rudp_send │   │ recvfrom       │
 │ welcome     │   │                  │   │ UDP send  │   │ channel_recv   │
 └─────────────┘   └──────────────────┘   └───────────┘   └───────┬────────┘
                                                                   │
                   ┌──────────────────┐   ┌───────────┐            │
                   │ Client Gameplay  │◀──│ topic_pop │◀───────────┘
                   │ (spawn entities, │   │ or direct │   topic_push
                   │  interpolate)    │   │ pop_msg   │
                   └──────────────────┘   └───────────┘
```
