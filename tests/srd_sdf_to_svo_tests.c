/**
 * @file srd_sdf_to_svo_tests.c
 * @brief Tests for SDF grid → SVO conversion.
 *
 * TDD Phase 1 (RED): tests define the API before implementation.
 */
#include "ferrum/procgen/srd/srd_sdf_to_svo.h"
#include "ferrum/procgen/srd/srd_sdf_grid.h"
#include "ferrum/npc/npc_svo.h"

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

/* ── Test harness ──────────────────────────────────────────────── */

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        return 1; \
    } \
} while (0)

#define ASSERT_INT_EQ(exp, act) do { \
    int _e = (exp), _a = (act); \
    if (_e != _a) { \
        fprintf(stderr, "  FAIL %s:%d: expected %d, got %d\n", \
                __FILE__, __LINE__, _e, _a); \
        return 1; \
    } \
} while (0)

/* ── SVO query helper ──────────────────────────────────────────── */

/**
 * @brief Query whether a voxel at world position is solid in the SVO.
 *
 * Walks the octree from root to the leaf level, following child pointers.
 * Returns true if the leaf node has NPC_SVO_FLAG_SOLID set.
 */
static bool svo_is_solid(const npc_svo_grid_t *svo,
                         float wx, float wy, float wz) {
    if (!svo || !svo->nodes || svo->node_count < 1)
        return false;

    /* Compute voxel coordinates within the SVO bounds */
    float ox = wx - svo->world_bounds.min.x;
    float oy = wy - svo->world_bounds.min.y;
    float oz = wz - svo->world_bounds.min.z;

    uint32_t cells = 1u << svo->max_depth;
    float world_size = svo->world_bounds.max.x - svo->world_bounds.min.x;
    float wy_size = svo->world_bounds.max.y - svo->world_bounds.min.y;
    float wz_size = svo->world_bounds.max.z - svo->world_bounds.min.z;
    if (world_size < wy_size) world_size = wy_size;
    if (world_size < wz_size) world_size = wz_size;

    float cell_size = world_size / (float)cells;

    int vx = (int)(ox / cell_size);
    int vy = (int)(oy / cell_size);
    int vz = (int)(oz / cell_size);

    if (vx < 0 || vx >= (int)cells ||
        vy < 0 || vy >= (int)cells ||
        vz < 0 || vz >= (int)cells)
        return false;

    /* Walk octree from root (node 0) */
    uint32_t node_idx = 0;
    uint32_t half = cells >> 1;

    /* Local coordinates within current octant */
    uint32_t lx = (uint32_t)vx;
    uint32_t ly = (uint32_t)vy;
    uint32_t lz = (uint32_t)vz;

    for (uint32_t depth = 0; depth < svo->max_depth; depth++) {
        /* Determine which child octant (Morton order: z*4 + y*2 + x) */
        uint32_t cx = (lx >= half) ? 1 : 0;
        uint32_t cy = (ly >= half) ? 1 : 0;
        uint32_t cz = (lz >= half) ? 1 : 0;
        uint32_t child_idx = cz * 4 + cy * 2 + cx;

        uint32_t child_node = svo->nodes[node_idx].children[child_idx];
        if (child_node == NPC_SVO_INVALID_NODE) {
            /* No child → this region is empty (air) */
            return false;
        }

        node_idx = child_node;

        /* Update local coordinates for the child octant */
        if (cx) lx -= half;
        if (cy) ly -= half;
        if (cz) lz -= half;
        half >>= 1;
    }

    /* Reached leaf — check flags */
    return (svo->nodes[node_idx].flags & NPC_SVO_FLAG_SOLID) != 0;
}

/* ── Test: basic box room ──────────────────────────────────────── */

static int test_basic_room(void) {
    /* Create a 16x8x16 grid at 0.5m voxels = 8x4x8 meter world.
     * Carve a 4x2x4 meter room centered at (4, 2, 4).
     * Convert to SVO. Verify walls are solid, interior is air. */
    srd_sdf_grid_t grid;
    float origin[3] = {0.0f, 0.0f, 0.0f};
    srd_sdf_grid_init(&grid, 16, 8, 16, 0.5f, origin);

    /* Carve room: center (4,2,4), half (2,1,2) */
    srd_sdf_grid_stamp_box(&grid, 4.0f, 2.0f, 4.0f, 2.0f, 1.0f, 2.0f);

    npc_svo_grid_t svo;
    int rc = srd_sdf_to_svo(&grid, &svo);
    ASSERT_INT_EQ(0, rc);

    /* SVO should have been created with nodes */
    ASSERT_TRUE(svo.node_count > 1);

    /* Interior point (4, 2, 4) should be AIR (not solid) —
     * the room interior is carved out */
    ASSERT_TRUE(!svo_is_solid(&svo, 4.0f, 2.0f, 4.0f));

    /* Wall point: (0.25, 2, 4) should be SOLID —
     * far from the room, in the surrounding wall */
    ASSERT_TRUE(svo_is_solid(&svo, 0.25f, 2.0f, 4.0f));

    /* Another wall point: (4, 0.25, 4) — floor below room */
    ASSERT_TRUE(svo_is_solid(&svo, 4.0f, 0.25f, 4.0f));

    /* Just outside room edge: room spans X=[2,6].
     * Point at (1.75, 2, 4) should be solid (SDF > 0) */
    ASSERT_TRUE(svo_is_solid(&svo, 1.75f, 2.0f, 4.0f));

    /* Clearly inside room: (2.75, 2, 4) — one voxel past boundary */
    ASSERT_TRUE(!svo_is_solid(&svo, 2.75f, 2.0f, 4.0f));

    npc_svo_grid_destroy(&svo);
    srd_sdf_grid_destroy(&grid);
    return 0;
}

/* ── Test: empty grid (all solid) ──────────────────────────────── */

static int test_all_solid(void) {
    /* Grid with no carved rooms — everything is solid. */
    srd_sdf_grid_t grid;
    float origin[3] = {0.0f, 0.0f, 0.0f};
    srd_sdf_grid_init(&grid, 8, 8, 8, 0.5f, origin);

    npc_svo_grid_t svo;
    int rc = srd_sdf_to_svo(&grid, &svo);
    ASSERT_INT_EQ(0, rc);

    /* Center should be solid */
    ASSERT_TRUE(svo_is_solid(&svo, 2.0f, 2.0f, 2.0f));

    npc_svo_grid_destroy(&svo);
    srd_sdf_grid_destroy(&grid);
    return 0;
}

/* ── Test: all air ─────────────────────────────────────────────── */

static int test_all_air(void) {
    /* Grid filled with negative values — everything is air. */
    srd_sdf_grid_t grid;
    float origin[3] = {0.0f, 0.0f, 0.0f};
    srd_sdf_grid_init(&grid, 8, 8, 8, 0.5f, origin);
    srd_sdf_grid_fill(&grid, -1.0f);

    npc_svo_grid_t svo;
    int rc = srd_sdf_to_svo(&grid, &svo);
    ASSERT_INT_EQ(0, rc);

    /* Center should be air (not solid) */
    ASSERT_TRUE(!svo_is_solid(&svo, 2.0f, 2.0f, 2.0f));

    /* Node count should be minimal (just root, no solid voxels) */
    ASSERT_INT_EQ(1, (int)svo.node_count);

    npc_svo_grid_destroy(&svo);
    srd_sdf_grid_destroy(&grid);
    return 0;
}

/* ── Test: two rooms ───────────────────────────────────────────── */

static int test_two_rooms(void) {
    /* Two carved rooms with a wall between them. */
    srd_sdf_grid_t grid;
    float origin[3] = {0.0f, 0.0f, 0.0f};
    srd_sdf_grid_init(&grid, 32, 8, 16, 0.5f, origin);

    /* Room A: center (4, 2, 4), half (2, 1, 2) → X=[2,6] */
    srd_sdf_grid_stamp_box(&grid, 4.0f, 2.0f, 4.0f, 2.0f, 1.0f, 2.0f);
    /* Room B: center (12, 2, 4), half (2, 1, 2) → X=[10,14] */
    srd_sdf_grid_stamp_box(&grid, 12.0f, 2.0f, 4.0f, 2.0f, 1.0f, 2.0f);

    npc_svo_grid_t svo;
    srd_sdf_to_svo(&grid, &svo);

    /* Room A interior: air */
    ASSERT_TRUE(!svo_is_solid(&svo, 4.0f, 2.0f, 4.0f));
    /* Room B interior: air */
    ASSERT_TRUE(!svo_is_solid(&svo, 12.0f, 2.0f, 4.0f));
    /* Wall between rooms: solid */
    ASSERT_TRUE(svo_is_solid(&svo, 8.0f, 2.0f, 4.0f));

    npc_svo_grid_destroy(&svo);
    srd_sdf_grid_destroy(&grid);
    return 0;
}

/* ── Test: null inputs ─────────────────────────────────────────── */

static int test_null_inputs(void) {
    srd_sdf_grid_t grid;
    npc_svo_grid_t svo;
    float origin[3] = {0.0f, 0.0f, 0.0f};
    srd_sdf_grid_init(&grid, 4, 4, 4, 0.5f, origin);

    ASSERT_INT_EQ(-1, srd_sdf_to_svo(NULL, &svo));
    ASSERT_INT_EQ(-1, srd_sdf_to_svo(&grid, NULL));

    srd_sdf_grid_destroy(&grid);
    return 0;
}

/* ── Test: SVO bounds match grid ───────────────────────────────── */

static int test_svo_bounds(void) {
    /* Grid at origin (10, 5, 20), dims 16x8x16, voxel_size 0.25.
     * World spans [10, 14] x [5, 7] x [20, 24]. */
    srd_sdf_grid_t grid;
    float origin[3] = {10.0f, 5.0f, 20.0f};
    srd_sdf_grid_init(&grid, 16, 8, 16, 0.25f, origin);

    npc_svo_grid_t svo;
    srd_sdf_to_svo(&grid, &svo);

    /* SVO bounds should encompass the grid's world extent */
    ASSERT_TRUE(svo.world_bounds.min.x <= 10.0f);
    ASSERT_TRUE(svo.world_bounds.min.y <= 5.0f);
    ASSERT_TRUE(svo.world_bounds.min.z <= 20.0f);
    ASSERT_TRUE(svo.world_bounds.max.x >= 14.0f);
    ASSERT_TRUE(svo.world_bounds.max.y >= 7.0f);
    ASSERT_TRUE(svo.world_bounds.max.z >= 24.0f);

    npc_svo_grid_destroy(&svo);
    srd_sdf_grid_destroy(&grid);
    return 0;
}

/* ── Test runner ───────────────────────────────────────────────── */

struct test_case {
    const char *name;
    int (*fn)(void);
};

static struct test_case TESTS[] = {
    {"basic_room",   test_basic_room},
    {"all_solid",    test_all_solid},
    {"all_air",      test_all_air},
    {"two_rooms",    test_two_rooms},
    {"null_inputs",  test_null_inputs},
    {"svo_bounds",   test_svo_bounds},
};

int main(void) {
    size_t total = sizeof(TESTS) / sizeof(TESTS[0]);
    size_t passed = 0;

    fprintf(stderr, "srd_sdf_to_svo_tests: %zu tests\n", total);
    for (size_t i = 0; i < total; i++) {
        struct test_case *tc = &TESTS[i];
        fprintf(stderr, "  RUN  %s\n", tc->name);
        int rc = tc->fn();
        if (rc == 0) {
            fprintf(stderr, "  OK   %s\n", tc->name);
            passed++;
        } else {
            fprintf(stderr, "  FAIL %s\n", tc->name);
        }
    }
    fprintf(stderr, "\n%zu/%zu passed\n", passed, total);
    return (passed == total) ? 0 : 1;
}
