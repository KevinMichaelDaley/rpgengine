---
id: phys-008
status: open
deps: [phys-001]
links: [phys-000]
created: 2026-02-06T05:20:00.000000000-08:00
type: task
priority: 1
---
# Step 0.8: Contact and Manifold Structures

**Parent Epic:** phys-000 (Phase 0: Foundation Data Structures)

## Description

Define contact point and manifold structures. Manifolds hold up to 4 contact
points per body pair and store warmstart impulses for solver convergence.

## Files to create

- `include/ferrum/physics/manifold.h`
- `src/physics/collision/manifold.c`
- `tests/physics/manifold_tests.c`

## Structures

```c
typedef struct phys_contact_point_t {
    phys_vec3_t point_world;    // contact point in world space
    phys_vec3_t local_a;        // contact point in body A local space
    phys_vec3_t local_b;        // contact point in body B local space
    phys_vec3_t normal;         // points from A to B
    float penetration;          // positive = overlap
    uint32_t feature_id;        // for persistent contact tracking
} phys_contact_point_t;

#define PHYS_MAX_MANIFOLD_POINTS 4

typedef struct phys_manifold_t {
    uint32_t body_a;
    uint32_t body_b;
    uint8_t point_count;
    uint8_t pad[3];
    phys_contact_point_t points[PHYS_MAX_MANIFOLD_POINTS];
    
    // Material properties (combined from both bodies)
    float friction;
    float restitution;
    
    // Warmstarting data (from previous frame)
    float normal_impulse[PHYS_MAX_MANIFOLD_POINTS];
    float tangent_impulse[PHYS_MAX_MANIFOLD_POINTS][2];
} phys_manifold_t;
```

## API

```c
void phys_manifold_init(phys_manifold_t *m, uint32_t body_a, uint32_t body_b);
void phys_manifold_add_point(phys_manifold_t *m, const phys_contact_point_t *point);
void phys_manifold_reduce_points(phys_manifold_t *m);  // keep best 4 points
void phys_manifold_clear(phys_manifold_t *m);

// Combine materials (geometric mean for friction, min for restitution)
float phys_combine_friction(float f1, float f2);
float phys_combine_restitution(float r1, float r2);

// Feature ID computation
uint32_t phys_feature_id_edge(uint8_t face, uint8_t edge);
uint32_t phys_feature_id_face(uint8_t face);
uint32_t phys_feature_id_vertex(uint8_t vertex);
```

## Acceptance Criteria

- [ ] Manifold holds up to 4 contact points
- [ ] Feature IDs enable persistent tracking
- [ ] Point reduction keeps best contact configuration (deepest + most spread)
- [ ] Warmstart impulses stored per-point
- [ ] Material combination follows standard formulas

## Test Cases

```c
// test_manifold_add_points
phys_manifold_t m;
phys_manifold_init(&m, 1, 2);

phys_contact_point_t c = {
    .point_world = {0, 0, 0},
    .normal = {0, 1, 0},
    .penetration = 0.1f,
    .feature_id = 1
};

phys_manifold_add_point(&m, &c);
ASSERT(m.point_count == 1);

c.point_world = (phys_vec3_t){1, 0, 0};
c.feature_id = 2;
phys_manifold_add_point(&m, &c);
ASSERT(m.point_count == 2);

// test_manifold_reduce_keeps_four
for (int i = 0; i < 10; ++i) {
    c.point_world = (phys_vec3_t){i * 0.1f, 0, 0};
    c.feature_id = 100 + i;
    phys_manifold_add_point(&m, &c);
}
// Should have been reduced to 4
ASSERT(m.point_count == 4);

// test_manifold_reduce_keeps_deepest
phys_manifold_clear(&m);
c.penetration = 0.1f;
c.point_world = (phys_vec3_t){0, 0, 0};
phys_manifold_add_point(&m, &c);

c.penetration = 0.5f;  // deeper
c.point_world = (phys_vec3_t){1, 0, 0};
phys_manifold_add_point(&m, &c);

c.penetration = 0.01f;  // shallow
c.point_world = (phys_vec3_t){2, 0, 0};
phys_manifold_add_point(&m, &c);

c.penetration = 0.2f;
c.point_world = (phys_vec3_t){3, 0, 0};
phys_manifold_add_point(&m, &c);

c.penetration = 0.05f;  // 5th point, triggers reduce
c.point_world = (phys_vec3_t){4, 0, 0};
phys_manifold_add_point(&m, &c);

// Deepest point (0.5f) should be kept
bool found_deepest = false;
for (int i = 0; i < m.point_count; ++i) {
    if (m.points[i].penetration == 0.5f) found_deepest = true;
}
ASSERT(found_deepest);

// test_material_combination
ASSERT_FLOAT_NEAR(phys_combine_friction(0.5f, 0.5f), 0.5f, 0.001f);
ASSERT_FLOAT_NEAR(phys_combine_friction(0.25f, 1.0f), 0.5f, 0.001f);  // sqrt
ASSERT_FLOAT_NEAR(phys_combine_restitution(0.8f, 0.5f), 0.5f, 0.001f);  // min
```
