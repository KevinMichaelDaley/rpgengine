---
id: phys-113
status: open
deps: [phys-112]
links: [phys-100]
created: 2026-02-06T05:20:00.000000000-08:00
type: task
priority: 1
---
# Step 1.13: Integrate + Sleep Stage (Stage 12)

**Parent Epic:** phys-100 (Phase 1: Complete Pipeline)

## Description

Implement Stage 12: Integrate + Sleep. Updates positions from velocities
and detects sleeping bodies.

## Files to create

- `include/ferrum/physics/integrate.h`
- `src/physics/stages/integrate.c`
- `tests/physics/integrate_tests.c`

## API

```c
typedef struct phys_integrate_args_t {
    const phys_body_t *bodies_in;
    const phys_velocity_t *velocities;
    phys_body_t *bodies_out;
    uint32_t body_count;
    float dt;
    phys_vec3_t gravity;
    float sleep_threshold_linear;
    float sleep_threshold_angular;
} phys_integrate_args_t;

void phys_stage_integrate(const phys_integrate_args_t *args);
```

## Implementation

```c
void phys_stage_integrate(const phys_integrate_args_t *args) {
    for (uint32_t i = 0; i < args->body_count; ++i) {
        const phys_body_t *in = &args->bodies_in[i];
        phys_body_t *out = &args->bodies_out[i];
        
        // Copy body
        *out = *in;
        
        if (phys_body_is_static(in) || phys_body_is_kinematic(in)) {
            continue;  // don't integrate static/kinematic
        }
        
        // Apply solved velocity
        const phys_velocity_t *vel = &args->velocities[i];
        out->linear_vel = vel->linear;
        out->angular_vel = vel->angular;
        
        // Apply gravity (if not applied in constraint build)
        // out->linear_vel = vec3_add(out->linear_vel, vec3_scale(args->gravity, args->dt));
        
        // Integrate position
        out->position = vec3_add(in->position, vec3_scale(out->linear_vel, args->dt));
        
        // Integrate orientation (quaternion integration)
        phys_quat_t omega_q = {
            out->angular_vel.x * 0.5f,
            out->angular_vel.y * 0.5f,
            out->angular_vel.z * 0.5f,
            0.0f
        };
        phys_quat_t dq = quat_mul(omega_q, in->orientation);
        out->orientation.x = in->orientation.x + dq.x * args->dt;
        out->orientation.y = in->orientation.y + dq.y * args->dt;
        out->orientation.z = in->orientation.z + dq.z * args->dt;
        out->orientation.w = in->orientation.w + dq.w * args->dt;
        out->orientation = quat_normalize(out->orientation);
        
        // Sleep check
        float linear_speed = vec3_length(out->linear_vel);
        float angular_speed = vec3_length(out->angular_vel);
        
        if (linear_speed < args->sleep_threshold_linear &&
            angular_speed < args->sleep_threshold_angular) {
            // TODO: increment sleep counter, sleep after N frames
        }
    }
}
```

## Acceptance Criteria

- [ ] Positions updated by velocity * dt
- [ ] Quaternion integrated and normalized
- [ ] Static/kinematic bodies unchanged
- [ ] Sleep detection based on velocity thresholds
- [ ] Output buffer distinct from input

## Test Cases

```c
// test_integrate_position
// Body with velocity → position changes correctly

// test_integrate_rotation
// Body with angular velocity → orientation rotates

// test_integrate_static_unchanged
// Static body → position unchanged

// test_integrate_sleep_detection
// Slow body → marked for sleep (after delay)
```
