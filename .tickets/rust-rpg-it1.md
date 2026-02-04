---
id: rust-rpg-it1
status: open
deps: [rust-rpg-aqq]
links: []
created: 2026-01-17T18:02:49.677747618-08:00
type: epic
priority: 2
---
# P_007 — Networking & Replication (RUDP, Snapshot/Delta, Prediction)

## P_007 — Networking & Replication (RUDP, Snapshot/Delta, Prediction)

### Design Intent
Enable full entity and component state synchronization from a server-authoritative simulation using UDP with reliability channels, snapshot/delta replication, client-side prediction with reconciliation, time synchronization, and interest management. Maintain low latency for high-rate state (movement), while guaranteeing ordered delivery for critical gameplay (inventory, dialogue, quests).

### Specification
#### Transport & Channels
- Packet header: `protocol_id: u32`, `sequence: u16`, `ack: u16`, `ack_bits: u32` (unchanged).
- Channels:
   - Unreliable high-rate (movement/transform deltas, predicted state corrections).
   - Reliable ordered (inventory, quest/dialogue, entity create/destroy, chunked snapshots).
   - Optional priorities (document as hints if not strictly enforced).

#### Time Synchronization
- Client estimates server clock offset via periodic pings; offset = median half-RTT.
- Jitter buffer for interpolation windows; clamp drift to avoid visual pops.

#### Entity & Component Replication
- Server authoritative entities use `entity_t { index, generation }` IDs.
- Client maintains a ghost table mapping remote IDs to local handles.
- Replication messages:
   - Create: entity archetype + initial components.
   - Update: component deltas with schema IDs and bit-packed fields.
   - Destroy: invalidate generation; cleanup ghost.
- Component sync policies (examples):
   - Transform: quantized floats (pos, rot) on unreliable channel at high rate.
   - Physics corrections: unreliable corrections + reliable hard snaps on large error.
   - Inventory/quest/dialogue: reliable ordered.
   - AI state: reliable ordered behavior/goal transitions; optional unreliable timers/counters.
   - AI LOD state (optional): lower frequency, unreliable.
   - Animation state: unreliable time updates; reliable clip transitions.
   - Custom attributes (from scripting modules): reliable ordered create/update/delete after manifest registration.
   - Animation state: unreliable high-rate `time`/`blend_weight` updates; reliable ordered `clip_id` changes and animation graph transitions.

#### Snapshot/Delta Replication
- Baselines: server tracks per-client baseline snapshot ID.
- Delta: bit-packed changes from baseline (quantized, run-length or varint where helpful).
- Client ACKs snapshot IDs; server advances baseline when safe.
- Fallback: if baseline missing, send partial/full snapshot chunks reliably.

#### Interest Management
- Spatial partition (grid/hash) forms per-client interest sets.
- Budgeted bytes per tick; prioritize nearby/visible entities/components.
- Dirty-bit tracking: only changed components replicated.

#### Client-Side Prediction & Reconciliation
- Local inputs buffered with timestamps (ring buffer).
- Client predicts movement; on authoritative corrections: rewind to last confirmed state, re-simulate buffered inputs.
- Error thresholds to avoid micro-corrections; hard snaps on large divergence.

#### Security & Validation
- Validate protocol header and schema IDs; drop invalid.
- Clamp/validate client inputs on server; never trust client state.

#### Serialization Schema
- Component schemas registered with IDs; encode/decode functions bit-pack fields.
- Network byte order; avoid dynamic allocations during encode/decode.
 - Dynamic module manifest: reliable message registering `module_id` and attribute schemas (name, `attr_id`, `type_tag`, encoding). Clients persist manifest for decode.

### Implementation Steps
1. Header encode/decode + validation (protocol ID, sizes).
2. Channel abstraction (unreliable/reliable ordered) with send/recv rings.
3. Time sync pings and offset estimator.
4. Entity ghost table and ECS mapping (create/update/destroy).
5. Component schemas and bit-packed serializer for deltas (including `ai_state`, `animation_state`, and `custom_attributes`).
6. Baseline tracking and delta aggregation per client.
7. Reliable snapshot chunking and reassembly for baseline recovery (include manifests and custom attributes if needed).
8. Interest management set builder and per-tick budgeter.
9. Client prediction buffer + reconciliation logic.
10. RTT estimator and retransmit scheduling.
11. Ordered channel reassembly and delivery.
12. Instrumentation counters (bytes/tick, dropped/resent, baseline distance).

### Architectural Considerations
- Tick-driven `net_update(dt)`; no blocking recv.
- Deterministic tests via injected clock and scripted loss/jitter.
- Fixed-size buffers; avoid malloc in hot path.
- Clear ECS ownership: networking creates/updates ghost entities via world APIs.
- Quantization documented; epsilon comparisons in tests.

### Unit Tests (RED-first)
**Happy Path**
1. **Header encode/decode roundtrip**
   - pack then unpack header; fields preserved; byte order defined.
2. **ACK bitfield correctness**
   - receiving sequences updates `ack` and `ack_bits` as expected.
3. **Ordered delivery without loss**
   - receive 1,2,3 on ordered channel; deliver 1,2,3 immediately.
4. **Entity create/update/destroy replication**
   - server sends create + component init; client builds ghost; updates apply; destroy removes ghost.
5. **Delta from baseline**
   - given baseline snapshot, delta applies to reach target; client ACK advances baseline.
6. **Interest set update**
   - moving client changes interest set; replication set updates accordingly.
7. **Prediction + reconciliation**
   - client inputs predicted; authoritative correction arrives; re-simulated state matches server within epsilon.
8. **Time sync stability**
   - offset estimator converges; jitter buffer produces smooth interpolation times.
9. **Animation state replication**
   - server changes `clip_id` (reliable); client receives and switches; time updates arrive on unreliable channel; blended pose remains consistent.
10. **AI state replication**
   - server sends behavior/goal transition (reliable); client reflects state; optional timers arrive on unreliable channel; UI shows consistent state.
11. **Custom attributes replication**
   - server sends manifest; then create/update/delete attribute messages; client decodes and applies to ghost entity’s attribute table.

**Edge Cases**
9. **Sequence wraparound**
   - near `UINT16_MAX`, send/receive wrap to 0; ordering and ack logic remains correct.
10. **Duplicate packet**
   - receiving the same sequence twice is ignored and does not re-deliver.
11. **Out-of-window packet**
   - very old sequence is dropped and does not perturb ack window.
12. **Missing baseline fallback**
   - client missing baseline requests/receives snapshot chunks; state reconstructs deterministically.
13. **Partial component update**
   - only dirty fields replicated; client merges deltas correctly.
14. **Animation time wraparound**
   - clip looping wraps time; client applies wrap consistently with server.
15. **Manifest late arrival**
   - custom attribute update received before manifest is buffered or dropped; once manifest arrives, subsequent updates decode correctly.
16. **Attribute type evolution**
   - manifest version bump changes `type_tag`; client detects and requests fresh manifest; stale updates ignored.

**Failure Modes**
14. **Protocol ID mismatch**
   - wrong `protocol_id` must drop packet (no state changes).
15. **Malformed payload length**
   - too-short buffer for component schema returns explicit decode failure.
16. **Ring buffer exhaustion**
   - if send buffer is full, send returns explicit failure (or drops oldest, documented).
17. **Unknown schema ID**
   - decode rejects with error and skips update safely.
18. **Invalid animation parameters**
   - out-of-range `clip_id` or negative time rejected; state unchanged.
19. **Unknown module/attribute IDs**
   - updates for unknown `module_id/attr_id` dropped; error counters incremented.
20. **Malformed attribute payload**
   - decode failure returns error; ghost’s attribute table remains consistent.

### Regression Tests (RED-first)
1. **Resend logic**
   - drop seq 5; receive ack 6 with bit for 5 = 0; update triggers resend of 5.
2. **Ack-bits off-by-one**
   - confirm that `ack_bits` bit 0 corresponds to `ack-1` (or chosen mapping), locked by tests.
3. **RTT estimator stability**
   - repeated samples must converge; no negative RTT; variance remains bounded.
4. **Bit-packing layout lock-in**
   - schema encode/decode matches golden bytes.
5. **Quantization determinism**
   - transform quantization/dequantization remains stable across runs.
6. **Ghost leak prevention**
   - destroyed entities free ghost slots; no leaked handles across many churn cycles.
7. **Manifest schema lock-in**
   - golden bytes for manifest encode/decode; attribute tables reproduce identical state from the same stream.
8. **AI behavior/goal determinism**
   - replicated transitions produce identical client-side summaries across runs.

### Cumulative Integration Tests (RED-first, cumulative through P_007)
1. **Server→Client ECS replication (P_000..P_007)**
   - server world ticks and emits deltas; client applies to ghosts; verify component states match authoritative snapshot after reconciliation.
2. **Predicted player movement**
   - client buffers inputs, predicts, reconciles with corrections; final pose matches server within tolerance.
3. **Interest-managed bandwidth budget**
   - many entities; budget caps bytes/tick; nearest entities prioritized; verify deterministic selection and stable client state.
4. **AI + scripting attributes sync**
   - server loads a module, registers manifest, and updates custom attributes; client reflects attributes and AI state; reconciliation remains consistent.

---



