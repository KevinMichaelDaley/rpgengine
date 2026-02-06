---
id: phys-118
status: closed
deps: [phys-116]
links: [phys-100]
created: 2026-02-06T06:56:00.000000000-08:00
type: task
priority: 1
---
# Step 1.18: Client Prediction Reconciliation

**Parent Epic:** phys-100 (Phase 1: Complete Pipeline)

## Description

Implement client-side prediction and server reconciliation for networked
physics. Clients run local physics simulation, receive authoritative server
snapshots, and reconcile differences.

This is critical for responsive networked gameplay—players see immediate
physics response while the server maintains authority.

## Files to create

- `include/ferrum/physics/prediction.h`
- `src/physics/net/prediction.c`
- `tests/physics/prediction_tests.c`

## Structures

```c
typedef struct phys_prediction_config_t {
    float position_snap_threshold;   // snap if error > this (meters)
    float position_blend_rate;       // lerp rate for small errors
    float rotation_snap_threshold;   // snap if error > this (radians)
    float rotation_blend_rate;       // slerp rate for small errors
    float velocity_correction_rate;  // how fast to correct velocity
    uint32_t max_rewind_ticks;       // max ticks to re-simulate
} phys_prediction_config_t;

typedef struct phys_prediction_error_t {
    pool_handle_t body;
    phys_vec3_t position_error;   // predicted - server
    float rotation_error;          // angle difference
    phys_vec3_t velocity_error;
} phys_prediction_error_t;

typedef struct phys_prediction_result_t {
    uint32_t bodies_snapped;       // bodies that were teleported
    uint32_t bodies_blended;       // bodies that were smoothly corrected
    uint32_t bodies_correct;       // bodies within tolerance
    float max_position_error;
    float max_rotation_error;
    phys_prediction_error_t *errors;  // arena-allocated details
    uint32_t error_count;
} phys_prediction_result_t;
```

## API

```c
// Default config with reasonable values
phys_prediction_config_t phys_prediction_config_default(void);

// Reconcile local state with server snapshot
// Returns details about what was corrected
phys_prediction_result_t phys_prediction_reconcile(
    phys_world_t *local_world,
    const phys_snapshot_t *server_snapshot,
    const phys_prediction_config_t *config,
    phys_frame_arena_t *arena
);

// Apply smooth correction over multiple frames
// Called each tick after reconcile to blend out errors
void phys_prediction_apply_blend(
    phys_world_t *world,
    const phys_prediction_result_t *result,
    float dt
);

// Check if body needs re-simulation
bool phys_prediction_body_diverged(
    const phys_body_t *local,
    const phys_snapshot_body_t *server,
    const phys_prediction_config_t *config
);

// Full rewind-and-replay for significant divergence
// Re-simulates from server state applying stored inputs
int phys_prediction_rewind_replay(
    phys_world_t *world,
    const phys_snapshot_t *server_snapshot,
    const phys_input_buffer_t *input_buffer,  // stored player inputs
    uint64_t server_tick,
    uint64_t current_tick
);
```

## Algorithm

```
1. Receive server snapshot for tick T
2. Compare server state to local predicted state at tick T
3. For each body:
   a. Compute position/rotation error
   b. If error < blend_threshold:
      - Add to blend list (will be smoothed over next N frames)
   c. If error > snap_threshold:
      - Snap immediately to server state
   d. Else:
      - Blend at configured rate
4. If any body snapped or large errors detected:
   - Consider rewind-replay from server tick T to current tick
   - Re-apply stored player inputs
5. Return reconciliation result for debugging/telemetry
```

## Acceptance Criteria

- [ ] Small errors (< 5cm) are smoothly blended
- [ ] Large errors (> 50cm) snap immediately
- [ ] Rotation errors handled with slerp
- [ ] Reconciliation preserves velocity direction
- [ ] Rewind-replay can re-simulate up to 10 ticks
- [ ] Result provides debugging telemetry

## Test Cases

```c
// test_reconcile_no_error
// Local and server match exactly
// Result: all bodies_correct, no snaps, no blends

// test_reconcile_small_position_error
// Local body is 2cm off from server
// Result: body added to blend list, not snapped

// test_reconcile_large_position_error
// Local body is 1m off from server
// Result: body snapped immediately

// test_reconcile_rotation_error
// Local body rotated 5° from server
// Result: smooth slerp correction

// test_blend_reduces_error
// After reconcile with small error, call apply_blend multiple times
// Error should decrease each frame

// test_rewind_replay
// Server snapshot is 5 ticks old
// Rewind to server state, replay 5 ticks of stored input
// Final state should be closer to continuous prediction
```

## Network Integration

```c
// Client tick loop
void client_physics_tick(client_t *client) {
    // 1. Receive any server snapshots
    if (reliable_channel_recv(&snapshot_data)) {
        phys_snapshot_decode(&server_snapshot, snapshot_data);
        
        // 2. Reconcile with server
        phys_prediction_result_t result = phys_prediction_reconcile(
            &client->world,
            &server_snapshot,
            &client->prediction_config,
            &client->frame_arena
        );
        
        // 3. Log significant corrections for debugging
        if (result.max_position_error > 0.1f) {
            log_prediction_error(&result);
        }
    }
    
    // 4. Store current input for potential replay
    input_buffer_push(&client->inputs, current_tick, &player_input);
    
    // 5. Run local prediction tick
    phys_world_tick(&client->world);
    
    // 6. Apply any ongoing blend corrections
    phys_prediction_apply_blend(&client->world, &last_result, dt);
}
```
