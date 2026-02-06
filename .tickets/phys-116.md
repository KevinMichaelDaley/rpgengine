---
id: phys-116
status: open
deps: [phys-115]
links: [phys-100]
created: 2026-02-06T05:20:00.000000000-08:00
type: task
priority: 1
---
# Step 1.16: Network Snapshot Encoding

**Parent Epic:** phys-100 (Phase 1: Complete Pipeline)

## Description

Implement network snapshot encoding/decoding for physics state replication.
Uses existing engine quantization (vec3_mm, quat_snorm16).

## Files to create

- `include/ferrum/physics/snapshot.h`
- `src/physics/net/snapshot_encode.c`
- `src/physics/net/snapshot_decode.c`
- `tests/physics/snapshot_tests.c`

## Structures

```c
typedef struct phys_snapshot_body_t {
    int16_t position[3];     // quantized position (mm)
    int16_t orientation[3];  // smallest-3 quaternion
    int16_t linear_vel[3];   // quantized velocity
    int16_t angular_vel[3];  // quantized angular velocity
    uint8_t flags;           // sleeping, tier, etc.
} phys_snapshot_body_t;  // 21 bytes

typedef struct phys_snapshot_t {
    uint64_t tick;
    uint32_t body_count;
    phys_snapshot_body_t *bodies;
} phys_snapshot_t;
```

## API

```c
// Full snapshot
size_t phys_snapshot_encode(const phys_world_t *world, 
                             uint8_t *buffer, size_t max_size);
int phys_snapshot_decode(phys_world_t *world, 
                          const uint8_t *buffer, size_t size);

// Delta snapshot (only changed bodies)
size_t phys_snapshot_encode_delta(const phys_snapshot_t *prev, 
                                   const phys_world_t *world,
                                   uint8_t *buffer, size_t max_size,
                                   float position_threshold,
                                   float angle_threshold);
int phys_snapshot_decode_delta(const phys_snapshot_t *base,
                                phys_world_t *world,
                                const uint8_t *buffer, size_t size);

// Quantization
void phys_quantize_position(phys_vec3_t pos, int16_t out[3], float scale);
phys_vec3_t phys_dequantize_position(const int16_t in[3], float scale);
void phys_quantize_quat(phys_quat_t q, int16_t out[3]);
phys_quat_t phys_dequantize_quat(const int16_t in[3]);
```

## Delta Encoding

Only encode bodies that have changed beyond threshold:
- Position changed > 1mm
- Orientation changed > 0.1°
- Flags changed

Format:
```
[tick: u64][count: u16][body_id: u16, data: 21 bytes]...
```

## Acceptance Criteria

- [ ] Full snapshot encodes all bodies
- [ ] Delta only encodes changed bodies
- [ ] Round-trip error < 1mm position, < 0.1° rotation
- [ ] Sleeping bodies replicated correctly
- [ ] Size < 100 bytes/body full, < 30 bytes/body delta

## Test Cases

```c
// test_snapshot_round_trip
phys_world_t world1, world2;
// Setup world1 with 10 bodies

uint8_t buffer[4096];
size_t size = phys_snapshot_encode(&world1, buffer, 4096);
phys_snapshot_decode(&world2, buffer, size);

// Compare bodies
for each body:
    ASSERT_VEC3_NEAR(world1.pos, world2.pos, 0.001f);
    ASSERT_QUAT_NEAR(world1.rot, world2.rot, 0.001f);

// test_snapshot_delta_only_changed
// Move 1 of 10 bodies
// Delta should only contain 1 body

// test_snapshot_size
// 100 bodies full < 2500 bytes
// 100 bodies delta (10% changed) < 300 bytes
```
