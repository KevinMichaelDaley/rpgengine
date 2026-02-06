---
id: phys-002
status: closed
deps: [phys-001]
links: [phys-000]
created: 2026-02-06T05:20:00.000000000-08:00
type: task
priority: 1
---
# Step 0.2: Rigid Body Structure

**Parent Epic:** phys-000 (Phase 0: Foundation Data Structures)

## Description

Define the core rigid body structure with all fields needed for full simulation:
position, orientation, velocities, mass, inertia, flags, and tier.

## Files to create

- `include/ferrum/physics/body.h`
- `src/physics/body/body.c`
- `tests/physics/body_tests.c`

## Structure

```c
typedef struct phys_body_t {
    phys_vec3_t position;         // 12 bytes
    phys_quat_t orientation;      // 16 bytes
    phys_vec3_t linear_vel;       // 12 bytes
    phys_vec3_t angular_vel;      // 12 bytes
    float inv_mass;               // 4 bytes
    phys_vec3_t inv_inertia_diag; // 12 bytes (diagonal approx)
    uint32_t flags;               // 4 bytes (sleeping, static, kinematic)
    uint8_t tier;                 // 1 byte
    uint8_t sleep_counter;        // 1 byte (consecutive low-velocity frames)
    uint8_t pad[6];               // alignment
} phys_body_t;                    // 80 bytes

#define PHYS_BODY_FLAG_STATIC    (1 << 0)
#define PHYS_BODY_FLAG_KINEMATIC (1 << 1)
#define PHYS_BODY_FLAG_SLEEPING  (1 << 2)
```

## API

```c
void phys_body_init(phys_body_t *body);
void phys_body_set_mass(phys_body_t *body, float mass);
void phys_body_set_box_inertia(phys_body_t *body, float mass, phys_vec3_t half_extents);
void phys_body_set_sphere_inertia(phys_body_t *body, float mass, float radius);
void phys_body_set_capsule_inertia(phys_body_t *body, float mass, float radius, float half_height);
bool phys_body_is_static(const phys_body_t *body);
bool phys_body_is_kinematic(const phys_body_t *body);
bool phys_body_is_sleeping(const phys_body_t *body);
void phys_body_set_sleeping(phys_body_t *body, bool sleeping);
```

## Acceptance Criteria

- [ ] Body structure exactly 80 bytes (with padding)
- [ ] Mass/inertia setters compute correct inverse values
- [ ] Static (inv_mass == 0) correctly identified
- [ ] Sleeping/kinematic flags work correctly
- [ ] Inertia formulas correct for all three primitive types

## Test Cases

```c
// test_body_init_zeroed
phys_body_t b;
phys_body_init(&b);
ASSERT(b.inv_mass == 0.0f);  // static by default
ASSERT(phys_body_is_static(&b));
ASSERT(!phys_body_is_sleeping(&b));

// test_set_mass_computes_inv_mass
phys_body_set_mass(&b, 2.0f);
ASSERT_FLOAT_NEAR(b.inv_mass, 0.5f, 0.001f);
ASSERT(!phys_body_is_static(&b));

// test_sphere_inertia
phys_body_set_sphere_inertia(&b, 1.0f, 0.5f);
// I = 2/5 * m * r^2 = 0.1, inv = 10
ASSERT_FLOAT_NEAR(b.inv_inertia_diag.x, 10.0f, 0.1f);
ASSERT_FLOAT_NEAR(b.inv_inertia_diag.y, 10.0f, 0.1f);
ASSERT_FLOAT_NEAR(b.inv_inertia_diag.z, 10.0f, 0.1f);

// test_box_inertia
phys_body_set_box_inertia(&b, 1.0f, (phys_vec3_t){1, 2, 3});
// I_x = 1/12 * m * (h^2 + d^2) = 1/12 * 1 * (16 + 36) = 4.333
// inv_I_x = 0.231
ASSERT_FLOAT_NEAR(b.inv_inertia_diag.x, 0.231f, 0.01f);

// test_capsule_inertia
phys_body_set_capsule_inertia(&b, 1.0f, 0.5f, 1.0f);
// Capsule inertia is combination of cylinder + hemisphere
// Just verify it's non-zero and reasonable
ASSERT(b.inv_inertia_diag.x > 0);

// test_struct_size
ASSERT(sizeof(phys_body_t) == 80);
```

---

## Audit Note (2026-02-06)

**Reopened:** Code-vs-spec audit found discrepancies fixed in this pass:

1. **sleep_counter field added** — The callgraph integrate stage (Stage 12)
   requires a per-body sleep counter to track consecutive frames below the
   velocity threshold. Added `uint8_t sleep_counter` to the struct, reducing
   pad from 7 to 6 bytes (still 80 bytes total).

2. **Byte count corrected** — The ticket and physics.md both said "76 bytes →
   pad to 80" with `pad[3]`, but actual field total is 73 bytes, requiring
   `pad[7]` (now `pad[6]` with sleep_counter). Both docs corrected.

3. **Type names corrected** — physics.md used raw `vec3_t`/`quat_t` types;
   corrected to `phys_vec3_t`/`phys_quat_t` to match the implementation.

4. **Wake clears sleep_counter** — `phys_body_set_sleeping(body, false)` now
   resets `sleep_counter` to 0.

All existing tests pass after changes.
