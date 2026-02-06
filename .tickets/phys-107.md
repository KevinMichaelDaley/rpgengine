---
id: phys-107
status: open
deps: [phys-106]
links: [phys-100]
created: 2026-02-06T05:20:00.000000000-08:00
type: task
priority: 1
---
# Step 1.7: Sphere-Sphere Narrowphase (Stage 6)

**Parent Epic:** phys-100 (Phase 1: Complete Pipeline)

## Description

Implement Stage 6: Narrowphase for sphere-sphere collisions only.
Box and capsule added in Phase 2.

## Files to create

- `include/ferrum/physics/narrowphase.h`
- `src/physics/collision/narrowphase_sphere.c`
- `src/physics/stages/narrowphase.c`
- `tests/physics/narrowphase_sphere_tests.c`

## Structures

```c
typedef struct phys_contact_candidate_t {
    uint32_t body_a;
    uint32_t body_b;
    phys_contact_point_t contacts[PHYS_MAX_MANIFOLD_POINTS];
    uint8_t contact_count;
} phys_contact_candidate_t;
```

## API

```c
typedef struct phys_narrowphase_args_t {
    const phys_body_t *bodies;
    const phys_collider_t *colliders;
    const phys_sphere_t *spheres;
    const phys_box_t *boxes;
    const phys_capsule_t *capsules;
    const phys_collision_pair_t *pairs;
    uint32_t pair_count;
    phys_contact_candidate_t *candidates_out;
    uint32_t *candidate_count_out;
    uint32_t max_candidates;
} phys_narrowphase_args_t;

void phys_stage_narrowphase(const phys_narrowphase_args_t *args);

// Sphere-sphere test
bool phys_sphere_vs_sphere(
    phys_vec3_t center_a, float radius_a,
    phys_vec3_t center_b, float radius_b,
    phys_contact_point_t *contact_out);
```

## Sphere-Sphere Algorithm

```c
bool phys_sphere_vs_sphere(phys_vec3_t ca, float ra, phys_vec3_t cb, float rb,
                            phys_contact_point_t *out) {
    phys_vec3_t diff = vec3_sub(cb, ca);
    float dist_sq = vec3_length_sq(diff);
    float r_sum = ra + rb;
    
    if (dist_sq >= r_sum * r_sum) {
        return false;  // no contact
    }
    
    float dist = sqrtf(dist_sq);
    
    if (dist < 0.0001f) {
        // Overlapping centers, arbitrary normal
        out->normal = (phys_vec3_t){0, 1, 0};
        out->penetration = r_sum;
    } else {
        out->normal = vec3_scale(diff, 1.0f / dist);
        out->penetration = r_sum - dist;
    }
    
    // Contact point: midpoint of overlap
    out->point_world = vec3_add(ca, vec3_scale(out->normal, ra - out->penetration * 0.5f));
    out->feature_id = 0;  // spheres have no features
    
    return true;
}
```

## Acceptance Criteria

- [ ] Contact generated for overlapping spheres
- [ ] Normal points from A to B
- [ ] Penetration depth is positive when overlapping
- [ ] No contact for separated spheres
- [ ] Touching spheres (distance == r1 + r2) are contact
- [ ] Degenerate case (same center) handled

## Test Cases

```c
// test_sphere_overlap
phys_contact_point_t contact;
bool hit = phys_sphere_vs_sphere(
    (phys_vec3_t){0, 0, 0}, 1.0f,
    (phys_vec3_t){1.5f, 0, 0}, 1.0f,
    &contact);

ASSERT(hit);
ASSERT_FLOAT_NEAR(contact.penetration, 0.5f, 0.001f);
ASSERT_VEC3_NEAR(contact.normal, (phys_vec3_t){1, 0, 0}, 0.001f);

// test_sphere_separated
hit = phys_sphere_vs_sphere(
    (phys_vec3_t){0, 0, 0}, 1.0f,
    (phys_vec3_t){5, 0, 0}, 1.0f,
    &contact);
ASSERT(!hit);

// test_sphere_touching
hit = phys_sphere_vs_sphere(
    (phys_vec3_t){0, 0, 0}, 1.0f,
    (phys_vec3_t){2, 0, 0}, 1.0f,
    &contact);
ASSERT(hit);
ASSERT_FLOAT_NEAR(contact.penetration, 0.0f, 0.001f);

// test_sphere_coincident_centers
hit = phys_sphere_vs_sphere(
    (phys_vec3_t){0, 0, 0}, 1.0f,
    (phys_vec3_t){0, 0, 0}, 1.0f,
    &contact);
ASSERT(hit);
ASSERT(contact.penetration == 2.0f);
```
