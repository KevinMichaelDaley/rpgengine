# Networking Runtime Model (Client + Server)

This document defines where reliability, reassembly, and message dispatch live, and what other subsystems are allowed to see.

## Goals

- Ensure retransmission/reordering/reassembly happens before any gameplay system reads messages.
- Keep gameplay systems fiber-safe by never blocking on IO.
- Provide clear ownership of buffers and allocation strategy (arena/pool for gameplay-facing work).

## Layering (Strict)

1. **Socket IO (OS boundary)**
   - Owns OS socket handles.
   - Performs `recvfrom`/`sendto`.

2. **Protocol/frame parsing (wire boundary)**
   - Validates protocol id, header integrity, and extracts per-packet payload units.
   - Does not expose frames to gameplay subsystems.

3. **Reliability + reconstruction (stream boundary)**
   - Reliable: ack tracking, retransmit scheduling.
   - Reconstruction: in-order delivery for reliable streams, duplicate suppression, and reassembly of higher-level message units.
   - Output is an abstract per-channel **reliable UDP stream**.

4. **Channel/topic pump (engine boundary)**
   - Demultiplexes messages by topic/channel.
   - Writes message bytes into a long ring buffer per topic.

5. **Job dispatch (simulation boundary)**
   - Jobs subscribe to topics/channels.
   - Jobs read messages from ring buffers and apply them to game state.
   - Jobs are provided an arena allocator (with pool-backed reserve) for allocating entities/state created by server commands.

## Client Model

- **RX / reassembly thread**
  - Reads UDP packets.
  - Updates reliable state (ack windows) and reconstructs per-channel streams.
  - Pumps decoded messages into topic ring buffers.
  - Wakes/schedules jobs subscribed to those topics.

- **TX thread**
  - Polls outbound topic ring buffers.
  - Packages messages into packets, applies reliability framing, and transmits.

Notes:
- Gameplay systems never call socket APIs.
- Gameplay systems never parse protocol frames.

## Server Model

- **One fiber per client (job system scheduled)**
  - Maintains fiber-local per-client channels:
    - `reliable` (events/commands)
    - `unreliable` (high-rate state)
  - Reads packets from the client, reconstructs streams per channel, and publishes decoded inputs to a global update queue.
  - Serializes outbound updates from per-client channels.

### Player Connections vs Entities

Do not assume “connected client” == “spawnable player entity”.

- Many entities (NPCs, props, items, projectiles) can spawn without any notion of “join”.
- Some players may join and exist, but should not spawn to every other client (e.g., invisibility, distance-based interest).

Model player connectivity separately as a plain-data record:

- `player_connection_t`
  - `player_id`
  - `world_pos` (stored by value, not a reference)
  - `player_should_spawn_remote` (used by interest/visibility rules)

Simulation/entity subsystems use `player_connection_t` to decide when to emit join/spawn events and which outbound per-client topic(s) should receive them.

### Event Taxonomy (Server)

Server-side gameplay/simulation jobs should deal in high-level events, not protocol frames:

- `EVT_PLAYER_JOIN`: a connected player is accepted (drives welcome/spawn setup).
- `EVT_PLAYER_SPAWN`: a player should be spawned for some remote client(s) (interest/visibility gated).
- `EVT_ENTITY_JOIN`: reserved for non-player remote processes that “join” the server (audit/observer/bridge clients).
- `EVT_ENTITY_SPAWN`: non-player entity spawns (NPCs/props/etc).

The per-client fiber networking runtime remains responsible only for:
- reliable/unreliable reconstruction
- publishing decoded inbound messages to the global inbound topic/queue
- pumping per-client outbound topics into UDP packets

- **Global update queue**
  - A lock-free or low-contention queue of decoded messages (or message references) consumed by simulation jobs.

- **Threading requirement**
  - At least **one** dedicated OS worker thread in the networking job system for client fiber progress. For higher client counts, increase to 2+.
  - A separate **UDP receive thread** (`net_pump_thread`) runs `recvfrom` in a loop and pushes decoded packets into topic channels, independent of both job systems.
  - Client fiber stacks must be at least 256KB since `fr_server_client_fiber_main` stack-allocates ~68KB (inbox + send_slots).

## Memory / Ownership Rules

- Network threads/fibers may use `malloc`/`free` for internal buffering; they do not run on gameplay fibers.
- Jobs that apply game state changes should allocate through provided arenas/pools.
- No hidden global state: explicit context objects for network runtime, channel registry, and allocator sources.
