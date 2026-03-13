# Networking Runtime Model (Client + Server)

This document defines where reliability, reassembly, and message dispatch live, and what other subsystems are allowed to see.

## Goals

- Ensure retransmission/reordering/reassembly happens before any gameplay system reads messages.
- Keep gameplay systems fiber-safe by never blocking on IO.
- Provide clear ownership of buffers and allocation strategy (arena/pool for gameplay-facing work).

## Layering (Strict)

1. **Socket IO (OS boundary)**
   - Owns OS socket handles (`net_udp_socket_t`).
   - Performs `recvfrom`/`sendto` via `src/net/udp/socket_io.c` (also `socket_connected_io.c` for connected UDP).
   - Supports socket options, buffer sizing, address binding, and lifecycle management
     (`socket_lifecycle.c`, `socket_options.c`, `socket_buffer.c`, `socket_addr.c`, `socket_bind.c`).
   - When built with `FR_NET_EMULATION` (`make EMU=1`), sendto routes packets
     through a delay queue (`net_emulator_t`) configured via engine settings.
     Supports configurable latency, jitter (uniform/normal/log-normal distributions),
     packet loss, reorder, and duplicate percentages.  The emulator uses a 512-slot
     packet queue with xorshift32 PRNG and configurable distribution
     (`NET_EMU_DIST_UNIFORM`, `NET_EMU_DIST_NORMAL`, `NET_EMU_DIST_LOG_NORMAL`).
     Runtime reconfiguration via `net_emulator_configure()` is supported.

2. **Protocol/frame parsing (wire boundary)**
   - Validates protocol id, header integrity, and extracts per-packet payload units.
   - Packet header (`net_packet_header_t`, 40 bytes):
     `[protocol_id:u32][sequence:u16][ack:u16][ack_bits:4x u64 = 32 bytes]`
   - Wire frame header (`net_rudp_wire_frame_view_t`, 8 bytes):
     `[flags:u8][reserved:u8][schema_id:u16][payload_size:u16][reserved:u16]`
   - Flags: `NET_RUDP_WIRE_FLAG_RELIABLE` (0x01), `NET_RUDP_WIRE_FLAG_FRAGMENT` (0x02).
   - Protocol validation module (`net_validation_ctx_t`) checks minimum packet size,
     protocol ID match, schema whitelist, and payload consistency.  Maintains
     per-category instrumentation counters (`net_validation_stats_t`).
   - Does not expose frames to gameplay subsystems.

3. **Reliability + reconstruction (stream boundary)**
   - ACK window tracking (`net_ack_window_t`): 256-bit bitfield (4 x uint64), tracks up to
     256 recent sequence numbers.  Duplicate and out-of-window packets are rejected.
   - Reliable: ack tracking, retransmit scheduling via `net_rudp_peer_tick_resend()`.
     RTT measured via EWMA (alpha=0.125) on each ACK.  Stale send slots are expired
     by `max_slot_age_ms` to prevent permanent accumulation.
   - Fragmentation: optional per-peer via `net_rudp_peer_enable_fragmentation()`.
     Uses 64-bit fragment mask for up to 64 fragments per message.  Reassembly buffer
     is caller-provided (default 4096 bytes inline storage).
   - Reconstruction: in-order delivery for reliable streams, duplicate suppression.
   - Two reliable channel abstractions:
     - **`net_reliable_channel_t`**: fixed-size slot-based reliable ordered channel.
       Sequence-indexed, supports explicit resend.
     - **`net_reliable_ordered_channel_t`**: higher-level reliable ordered delivery
       with built-in fragmentation/reassembly and retransmit timeout.
   - Unreliable channel (`net_unreliable_channel_t`): ring buffer for high-rate packets.
   - Output is an abstract per-channel **reliable UDP stream** (`fr_rudp_stream_t`).

4. **Stream abstraction (fr_rudp_stream_t)**
   - Opaque reliable UDP stream context providing push/pop message interface per channel.
   - Inbound: `fr_rudp_stream_push_frame()` accepts raw frames
     `[seq:u16 LE][chan:u16 LE][payload]` and reassembles in-order per channel.
   - Outbound: `fr_rudp_stream_send()` queues messages;
     `fr_rudp_stream_flush_send()` serializes and transmits via sendto callback.
   - Configurable: channel count, slot count per channel, max payload size.
   - Optional topic integration: decoded messages can be auto-pumped to `fr_topic_channel_t` instances.

5. **Channel/topic pump (engine boundary)**
   - Topic channels (`fr_topic_channel_t`): ring buffer per topic with configurable
     capacity (messages and bytes), max message size, and backpressure policy:
     - `FR_TOPIC_BACKPRESSURE_FAIL`: push returns false on full.
     - `FR_TOPIC_BACKPRESSURE_DROP_OLDEST`: evicts oldest messages to make room.
     - `FR_TOPIC_BACKPRESSURE_DROP_NEWEST`: silently drops the new message.
   - Tracks drop count via `fr_topic_channel_stat_dropped()`.
   - Bit-pack header format for topic messages: `[schema_id:u16 BE][payload_size:u16 BE][payload]`.

6. **Topic dispatcher (simulation boundary)**
   - `fr_topic_dispatcher_t` binds topic channels to a job system.
   - Registers per-topic handlers with priority and worker affinity.
   - Background pump thread drains topics and dispatches to registered handlers.
   - Jobs read messages from topic channels and apply them to game state.
   - Jobs are provided an arena allocator (with pool-backed reserve) for allocating
     entities/state created by server commands.

## Schema Registry

The `net_schema_registry_t` maps schema IDs to expected fixed payload sizes and validates
incoming packets.  Up to 256 schemas.  Packet format:
`[schema_id:u16 BE][payload_size:u16 BE][payload]`.

All schema IDs are defined in `include/ferrum/net/replication/common.h`:

| Schema ID | Name | Direction | Reliability |
|-----------|------|-----------|-------------|
| 0x2001 | JOIN | client->server | reliable |
| 0x2002 | SPAWN | server->client | reliable |
| 0x2003 | STATE_CUBE | server->client | unreliable |
| 0x2004 | WELCOME | server->client | reliable |
| 0x2005 | SPAWN_BATCH | server->client | reliable |
| 0x2006 | STATE_CUBE_BATCH | server->client | unreliable |
| 0x2007 | INPUT_ROT | client->server | reliable |
| 0x2008 | INPUT_MOVE | client->server | reliable |
| 0x2009 | INPUT_SPAWN | client->server | reliable |
| 0x200A | BODY_SPAWN | server->client | reliable |
| 0x200B | BODY_STATE | server->client | unreliable |
| 0x200C | STREAM_FRAME | both | reliable (framing) |
| 0x200D | EVENT | server->client | reliable |
| 0x200E | BODY_STATE_BATCH | server->client | unreliable |
| 0x200F | SNAPSHOT_CHUNK | server->client | reliable |
| 0x2010 | MESH_DATA | server->client | reliable (chunked) |

## Client Model

- **RX runtime (`fr_client_rx_t`)**
  - Dedicated RX thread managed by `fr_client_rx_start()` / `fr_client_rx_stop()`.
  - Reads UDP packets (via socket or test callback `recv_cb`).
  - Updates reliable state (ack windows) and reconstructs per-channel streams.
  - Pumps decoded messages into topic channels (optional topic array in config).
  - Also supports manual frame injection for testing (`fr_client_rx_inject()`).
  - Configurable: max channels (default 16), max pending per channel (default 64).
  - Can bind an internal UDP socket via `fr_client_rx_bind_ipv4()`.

- **TX runtime (`fr_client_tx_t`)**
  - Dedicated TX pump thread managed by `fr_client_tx_start()` / `fr_client_tx_stop()`.
  - Game jobs enqueue messages via `fr_client_tx_enqueue()` (thread-safe).
  - Pump drains outbound stream through a sendto callback.
  - Rate limiting: `max_packets_per_pump` caps packets flushed per cycle.
  - Manual pump for testing: `fr_client_tx_pump_once()`.
  - Configurable: max channels (default 4), max pending per channel (default 64),
    max payload size (default 1024).

Notes:
- Gameplay systems never call socket APIs.
- Gameplay systems never parse protocol frames.

## Server Model

- **Server network runtime (`fr_server_net_runtime_t`)**
  - Manages per-client fiber jobs with reliable + unreliable streams.
  - Single demux pump (`fr_server_net_runtime_pump()`) receives UDP datagrams and
    routes them to per-client inboxes by transport key (FNV1a hash of sockaddr).
  - Each client gets a `fr_server_client_t` with:
    - Active flag and remote address.
    - Lock-free SPSC inbox (`fr_server_client_inbox_t`, 64 slots x 512 bytes).
    - One-packet staging area for the first packet before the fiber is ready.
    - Per-client outbound topics: `out_reliable` and `out_unreliable`.
    - Pre-allocated RUDP send slots.
    - Atomic stop flag and current time.
  - Optional test callbacks override recvfrom/sendto.

- **One fiber per client (job system scheduled)**
  - Entry point: `fr_server_client_fiber_main()`.
  - Stack-allocates inbox, RUDP peer, and outbound stream.
  - Demuxes inbound packets: RUDP protocol ID -> peer -> publish to global inbound topic;
    raw UDP (schema_id prefix) -> publish directly to inbound topic.
  - Outbound reliable: drains `out_reliable` topic -> stream channel 0 ->
    flush as RUDP frames with schema `NET_REPL_SCHEMA_STREAM_FRAME`.
  - Outbound unreliable: drains `out_unreliable` topic -> sends via RUDP peer.
  - Runs RUDP resend each iteration.
  - Yields after each loop iteration via `job_yield()`.

- **Global inbound topic**
  - Published by per-client fibers.
  - Message format:
    ```
    [client_id:u16 LE] [schema_id:u16 LE] [flags:u8 (bit0=reliable)] [reserved:u8] [payload...]
    ```
  - Formalized by `fr_server_net_inbound_message_view_t`.

- **State update queue (`fr_state_update_queue_t`)**
  - Lock-free multi-producer multi-consumer queue bridging network IO to simulation.
  - Non-blocking push/pop with explicit capacity and max payload size.
  - Messages carry `client_id`, `schema_id`, and payload bytes.

### Player Connections vs Entities

Do not assume "connected client" == "spawnable player entity".

- Many entities (NPCs, props, items, projectiles) can spawn without any notion of "join".
- Some players may join and exist, but should not spawn to every other client
  (e.g., invisibility, distance-based interest).

Model player connectivity separately as a plain-data record:

- `player_connection_t` (`include/ferrum/server/player/connection.h`)
  - `player_id` (u16)
  - `world_pos` (vec3_t, stored by value, not a reference)
  - `player_should_spawn_remote` (bool, used by interest/visibility rules)

Simulation/entity subsystems use `player_connection_t` to decide when to emit
join/spawn events and which outbound per-client topic(s) should receive them.

### Entity Net Pump (`fr_server_entity_net_pump_t`)

Consumes decoded inbound messages from the networking runtime and produces:
- High-level player/entity events to event topics.
- Outbound replication messages enqueued onto per-client outbound topics.

Event codes:
- `FR_SERVER_EVT_PLAYER_JOIN` (1)
- `FR_SERVER_EVT_PLAYER_SPAWN` (2)
- `FR_SERVER_EVT_ENTITY_JOIN` (3)
- `FR_SERVER_EVT_ENTITY_SPAWN` (4)
- `FR_SERVER_EVT_ENTITY_INPUT_ROT` (5)
- `FR_SERVER_EVT_ENTITY_INPUT_MOVE` (6)
- `FR_SERVER_EVT_ENTITY_INPUT_SPAWN` (7)

Handles JOIN messages by sending WELCOME + spawn burst on the reliable topic.
Supports per-client `player_should_spawn_remote` flag.

The per-client fiber networking runtime remains responsible only for:
- reliable/unreliable reconstruction
- publishing decoded inbound messages to the global inbound topic/queue
- pumping per-client outbound topics into UDP packets

- **Threading requirement**
  - At least **one** dedicated OS worker thread in the networking job system for
    client fiber progress. For higher client counts, increase to 2+.
  - A separate **UDP receive thread** (`net_pump_thread`) runs `recvfrom` in a loop
    and pushes decoded packets into per-client inboxes, independent of both job systems.
  - Client fiber stacks must be at least 256KB since `fr_server_client_fiber_main`
    stack-allocates ~68KB (inbox + send_slots + outbound stream).

## Body State Wire Format

Unreliable body state updates carry the full rigid body pose plus velocity:

```
[server_tick:u16] [body_id:u16] [pos_mm:3x i32] [rot_smallest3:7B]
[vel_mm_s:3x i16] [ang_mrad_s:3x i16] [send_time_ms:u32] [tick_time_ms:u32] [flags:u8]
```
Total: 44 bytes per body (`NET_REPL_BODY_STATE_PAYLOAD_SIZE`).

- **Velocity** (`vel_mm_s`, `ang_mrad_s`): server-authoritative linear (mm/s) and
  angular (mrad/s) velocity, quantized to int16 (plus/minus 32 m/s / plus/minus 32 rad/s range).
  Used by the pose interpolator for semi-physical interpolation.
- **send_time_ms**: server monotonic clock (truncated to u32 ms).  On localhost
  this shares the clock base with the client; with real latency it provides a
  lower bound on packet age.
- **tick_time_ms**: CLOCK_MONOTONIC timestamp (truncated to u32 ms) of when
  the physics tick that produced this position/orientation completed.  The client
  uses this to sample the interpolator at the exact physics moment for correction
  debug visualization (comparing interpolated pose vs true server pose at the
  same instant).
- **flags**: bit 0 = colliding, bits 4-6 = simulation tier (0-5).

### Body State Batching

Multiple body states are packed into a single `BODY_STATE_BATCH` message:
```
[count:u16 LE][body_state_0:44 bytes][body_state_1:44 bytes]...
```
Maximum entries per batch: 11 (fits in one RUDP unreliable packet: (464 - 2) / 44 = 10.5).
Max wire size: 486 bytes (2 + 11 x 44).

### Body State Inbox (Client-Side)

`fr_body_state_inbox_t` provides newest-wins semantics for unreliable body state:
for each `body_id`, only updates with a strictly newer `server_tick` are accepted.
Older (out-of-order) datagrams are silently ignored.

## Body Spawn Wire Format

Reliable body spawn message introducing a physics body to the client:

```
[body_id:u16] [flags:u8] [shape_type:u8] [color_seed:u32]
[pos_mm:3x i32] [rot_smallest3:7B] [half_extents:3x f16]
```
Total: 33 bytes (`NET_REPL_BODY_SPAWN_PAYLOAD_SIZE`).

- `shape_type`: 0=box, 1=sphere, 2=capsule, 3=mesh, 4=halfspace.
- Half-extents use float16 encoding for shape dimensions (meters).

## Mesh Data Replication

Large mesh geometry (FVMA format) is transferred via `MESH_DATA` (0x2010) reliable chunked messages:

Per-chunk wire layout:
```
[body_id:u16] [chunk_index:u16] [total_chunks:u16] [total_size:u32] [payload:N bytes]
```
Header: 10 bytes.  Max payload per chunk: 440 bytes (`NET_REPL_MESH_CHUNK_MAX`).
Max supported total mesh size: 256 KiB.

- **Server side**: `net_repl_mesh_data_send()` splits an FVMA blob into chunks and
  invokes a send callback for each.
- **Client side**: `net_repl_mesh_reassembly_table_t` tracks per-body reassembly with
  a 32-bit received mask (max 32 chunks per mesh).  Completion transfers ownership
  of the reassembled buffer to the caller.

## Join / Spawn Flow

1. Client sends `JOIN` (0x2001, 4 bytes: `client_nonce:u32`) via reliable RUDP.
2. Server entity net pump detects the JOIN, publishes `FR_SERVER_EVT_PLAYER_JOIN`.
3. Server sends `WELCOME` (0x2004, 4 bytes: `expected_entities:u16`, `tick_hz:u16`) on
   the reliable outbound topic.
4. Server sends a `SPAWN_BATCH` (0x2005) or individual `BODY_SPAWN` (0x200A) messages
   for all existing entities.
5. For each body with a mesh, `MESH_DATA` chunks are sent reliably.
6. Client processes WELCOME, creates entities from BODY_SPAWN, begins accepting
   BODY_STATE updates.

## Snapshot Delta Replication

For bandwidth-efficient state updates, the `net_snapshot_delta_t` system computes
field-level deltas between a baseline and current snapshot:

- Changed-field bitmask per body: position, orientation, linear velocity, angular
  velocity, flags, or destroy.
- Delta position mode uses higher precision (4096 scale, ~0.24mm, plus/minus 8m range)
  vs absolute (1000 scale, 1mm precision).
- Per-client baseline tracker (`net_snap_baseline_t`) with snapshot history ring buffer.
  Client ACKs advance the baseline; expired ACKs trigger full baseline recovery.

Snapshot chunking (`net_snapshot_chunk_split` / `net_chunk_reassembly_t`) splits large
snapshots into fixed-size chunks with 64-bit received mask for up to 64 chunks.

## Interest Management

`net_interest_query()` filters entities for per-client replication:
- Spatial proximity filter (configurable radius).
- Dirty flag check (only changed entities considered).
- Priority by distance (closer = higher priority, ties broken by entity_id).
- Per-tick byte budget enforcement (accumulates serialized sizes).

## Time Synchronization

`net_time_sync_t`: drift-clamped offset estimator using sliding-window median filter.
- Server embeds its timestamp in each outgoing packet.
- Client feeds `(server_time_ms, client_time_ms)` pairs.
- Offset = server_time - client_time, filtered to reject jitter outliers.
- Drift clamp limits how fast the applied offset changes per update.

`net_jitter_buffer_t`: tracks arrival jitter variance, produces a safety margin
for interpolation delay timing. Max-observed jitter in a 32-sample window.

## Quantization

Deterministic quantization helpers for networking (`include/ferrum/net/quantization.h`):
- **vec3 -> mm (i32)**: `net_quantize_vec3_mm()` / `net_dequantize_vec3_mm()`.
  Halves round away from zero.  Out-of-range rejected (no clamping).
- **quat -> snorm16**: `net_quantize_quat_snorm16()` / `net_dequantize_quat_snorm16()`.
  Sign-canonicalized (w >= 0), components clamped to [-1, 1].
- **float32 -> float16**: `net_float16_from_float()` / `net_float16_to_float()`.
  IEEE 754 binary16, range plus/minus 65504, nearest even rounding.
- **anim_time_u16**: wraparound addition and signed delta for animation time.
- **smallest-3 quaternion**: 7-byte wire encoding (`quat_smallest3.h`, `quat_smallest3.c`).

## Ghost Table

`net_ghost_table_t` maps server entity IDs to client-local entity handles
(`net_ghost_entity_t` with index + generation).  Fixed-capacity, caller-owned
storage, no dynamic allocation.  Linear scan lookup.

## Client-Side Prediction

`net_predict_ctx_t` (`include/ferrum/net/prediction.h`):
- Client stores inputs in a ring buffer keyed by tick.
- Each tick, applies local input to advance predicted state (optimistic simulation).
- On server-authoritative state arrival: rewind, replay unconfirmed inputs,
  compare to old prediction.  Error > snap_threshold -> hard snap; error >
  blend_threshold -> soft blend; otherwise no correction.
- Simulation step provided as a callback (`net_predict_sim_fn`), decoupling from physics.

`fr_prediction_tick_t` (`include/ferrum/net/replication/prediction_tick.h`):
- Dedicated prediction thread running at fixed timestep matching server physics rate.
- Triple-buffer reconciliation: bodies_curr (render reads), bodies_next (prediction writes),
  bodies_net (recv thread writes server data via atomic dirty flags).
- Each tick: copy curr -> next, reconcile from net (snap/blend), integrate (gravity,
  velocity, orientation), swap curr <-> next.
- Velocity damping (0.999/s), max linear speed 100 m/s, max angular speed 50 rad/s.

## Test Infrastructure

- **`net_test_link_t`**: deterministic in-process packet link with configurable loss,
  duplicate, reorder, and jitter.  Also supports `link_peek` for inspection.
- **`net_test_clock_t`**: deterministic clock for test time control.
- **`net_test_buffer_t`**: test buffer helpers.
- **`net_test_transport_t`**: test transport abstraction.
- **`fr_test_client_t`**: headless deterministic test client using test links,
  RUDP peer, and stream reassembly.  Single-threaded; caller drives pump functions.

## Memory / Ownership Rules

- Network threads/fibers may use `malloc`/`free` for internal buffering; they do not run on gameplay fibers.
- Jobs that apply game state changes should allocate through provided arenas/pools.
- No hidden global state: explicit context objects for network runtime, channel registry, and allocator sources.
- All validation/emulator/channel modules use caller-provided or inline storage; no hidden heap usage.
