#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "ferrum/physics/static_bvh.h"
#include "ferrum/physics/phys_pool.h"

static inline uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static int bench_build_10k_aabbs(void) {
    const uint32_t n = 10000u;

    phys_aabb_t *items = malloc((size_t)n * sizeof(*items));
    if (!items) {
        return 1;
    }

    /* Deterministic grid-ish distribution. */
    for (uint32_t i = 0; i < n; i++) {
        float x = (float)(i % 100u) * 2.0f;
        float z = (float)(i / 100u) * 2.0f;
        items[i].min = (phys_vec3_t){x, 0.0f, z};
        items[i].max = (phys_vec3_t){x + 1.0f, 1.0f, z + 1.0f};
    }

    phys_frame_arena_t arena;
    if (phys_frame_arena_init(&arena, 32u * 1024u * 1024u) != 0) {
        free(items);
        return 1;
    }

    phys_static_bvh_t bvh;
    uint64_t t0 = now_ns();
    phys_static_bvh_build(&bvh, items, NULL, n, &arena);
    uint64_t t1 = now_ns();

    double ms = (double)(t1 - t0) / 1e6;
    fprintf(stderr, "OK bench_build_10k_aabbs: %.3f ms (nodes=%u) target<50ms\n",
            ms, bvh.node_count);

    int rc = (ms < 50.0) ? 0 : 1;

    phys_frame_arena_destroy(&arena);
    free(items);
    return rc;
}

int main(void) {
    fprintf(stderr, "RUN p088_physics_static_bvh_build_benchmarks\n");
    return bench_build_10k_aabbs();
}
