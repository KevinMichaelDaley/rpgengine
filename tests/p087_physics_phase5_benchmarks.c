#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "ferrum/physics/collider.h"
#include "ferrum/physics/query.h"
#include "ferrum/physics/world.h"

static inline uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static int bench_1000_raycasts_1000_bodies(void) {
    phys_world_t world;
    phys_world_config_t cfg = phys_world_config_default();
    cfg.max_bodies = 1024u;
    cfg.max_colliders = 1024u;
    cfg.frame_arena_size = 8u * 1024u * 1024u;
    if (phys_world_init(&world, &cfg) != 0) {
        return 1;
    }

    /* Deterministic 10x10x10 grid of spheres. */
    uint32_t idx = 0;
    for (uint32_t z = 0; z < 10; z++) {
        for (uint32_t y = 0; y < 10; y++) {
            for (uint32_t x = 0; x < 10; x++) {
                uint32_t body = phys_world_create_body(&world);
                if (body == UINT32_MAX) {
                    phys_world_destroy(&world);
                    return 1;
                }
                phys_body_t *b = phys_world_get_body(&world, body);
                b->position = (phys_vec3_t){(float)x * 1.5f, (float)y * 1.5f, (float)z * 1.5f};
                phys_world_set_sphere_collider(&world, body, 0.4f, (phys_vec3_t){0, 0, 0});
                idx++;
            }
        }
    }

    const uint32_t rays = 1000;
    phys_ray_t ray = {
        .origin = (phys_vec3_t){-5.0f, 7.0f, 7.0f},
        .direction = (phys_vec3_t){1.0f, 0.0f, 0.0f},
        .max_distance = 100.0f,
    };

    uint64_t t0 = now_ns();
    uint32_t hit_count = 0;
    for (uint32_t i = 0; i < rays; i++) {
        ray.origin.y = 0.75f * (float)(i % 10);
        ray.origin.z = 0.75f * (float)((i / 10) % 10);
        phys_raycast_hit_t hit;
        if (phys_raycast(&world, &ray, &hit, 0xFFFFFFFFu)) {
            hit_count++;
        }
    }
    uint64_t t1 = now_ns();

    double ms = (double)(t1 - t0) / 1e6;
    fprintf(stderr, "OK bench_1000_raycasts_1000_bodies: %.3f ms (hits=%u) target<5ms\n",
            ms, hit_count);

    phys_world_destroy(&world);
    return 0;
}

static int bench_overlap_100_queries(void) {
    phys_world_t world;
    phys_world_config_t cfg = phys_world_config_default();
    cfg.max_bodies = 1024u;
    cfg.max_colliders = 1024u;
    cfg.frame_arena_size = 8u * 1024u * 1024u;
    if (phys_world_init(&world, &cfg) != 0) {
        return 1;
    }

    for (uint32_t i = 0; i < 500; i++) {
        uint32_t body = phys_world_create_body(&world);
        if (body == UINT32_MAX) {
            phys_world_destroy(&world);
            return 1;
        }
        phys_body_t *b = phys_world_get_body(&world, body);
        b->position = (phys_vec3_t){(float)(i % 25) * 1.0f, 0.0f, (float)(i / 25) * 1.0f};
        phys_world_set_sphere_collider(&world, body, 0.4f, (phys_vec3_t){0, 0, 0});
    }

    /* Query shape references world pool. */
    uint32_t si = world.sphere_count++;
    world.spheres[si].radius = 2.0f;
    phys_collider_t query;
    phys_collider_init_sphere(&query, si, (phys_vec3_t){0, 0, 0});

    const uint32_t queries = 100;
    uint64_t t0 = now_ns();
    uint32_t total_hits = 0;
    for (uint32_t i = 0; i < queries; i++) {
        phys_vec3_t p = (phys_vec3_t){(float)(i % 25), 0.0f, (float)((i / 5) % 20)};
        uint32_t out[64];
        uint32_t n = phys_overlap(&world, &query, p, (phys_quat_t){0, 0, 0, 1}, out, 64u, 0xFFFFFFFFu);
        total_hits += n;
    }
    uint64_t t1 = now_ns();

    double ms = (double)(t1 - t0) / 1e6;
    fprintf(stderr, "OK bench_overlap_100_queries: %.3f ms (hits=%u) target<1ms\n",
            ms, total_hits);

    phys_world_destroy(&world);
    return 0;
}

int main(void) {
    fprintf(stderr, "RUN p087_physics_phase5_benchmarks\n");

    int rc = 0;
    rc |= bench_1000_raycasts_1000_bodies();
    rc |= bench_overlap_100_queries();

    return rc;
}
