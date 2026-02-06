/**
 * @file p050_physics_impact_event_tests.c
 * @brief Unit tests for Impact Event Retrieval API (phys-119).
 *
 * Tests cover: empty initial state, add-and-retrieve, clear,
 * per-body filtering, strongest-impact lookup, and threshold config.
 */

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/physics/world.h"
#include "ferrum/physics/cache_commit.h"

/* ── Test macros ────────────────────────────────────────────────── */

#define ASSERT_TRUE(cond)                                                      \
    do {                                                                        \
        if (!(cond)) {                                                          \
            fprintf(stderr, "ASSERT_TRUE failed: %s:%d: %s\n",                 \
                    __FILE__, __LINE__, #cond);                                 \
            return 1;                                                           \
        }                                                                       \
    } while (0)

#define ASSERT_FLOAT_NEAR(exp, act, tol)                                       \
    do {                                                                        \
        float _e = (exp), _a = (act), _t = (tol);                              \
        if (fabsf(_e - _a) > _t) {                                             \
            fprintf(stderr, "ASSERT_FLOAT_NEAR failed: %s:%d: "               \
                    "expected %.6f got %.6f (tol %.6f)\n",                      \
                    __FILE__, __LINE__, (double)_e, (double)_a, (double)_t);    \
            return 1;                                                           \
        }                                                                       \
    } while (0)

#define RUN_TEST(fn)                                                           \
    do {                                                                        \
        printf("  %-50s", #fn);                                                 \
        int _r = fn();                                                          \
        printf("%s\n", _r ? "FAIL" : "PASS");                                   \
        if (_r) fail_count++;                                                   \
        test_count++;                                                           \
    } while (0)

/* ── Helpers ────────────────────────────────────────────────────── */

/** @brief Return a default world config suitable for testing. */
static phys_world_config_t test_config(void)
{
    phys_world_config_t cfg = phys_world_config_default();
    cfg.max_bodies = 16;
    cfg.max_colliders = 16;
    cfg.manifold_cache_size = 16;
    cfg.frame_arena_size = 4096;
    return cfg;
}

/** @brief Create and insert an impact event directly into the world buffer. */
static void push_event(phys_world_t *world, uint32_t body_a, uint32_t body_b,
                        float impulse)
{
    /* Directly manipulate the buffer for testing. */
    uint32_t idx = world->impact_event_count;
    world->impact_events[idx].body_a = body_a;
    world->impact_events[idx].body_b = body_b;
    world->impact_events[idx].point  = (phys_vec3_t){0.0f, 0.0f, 0.0f};
    world->impact_events[idx].normal = (phys_vec3_t){0.0f, 1.0f, 0.0f};
    world->impact_events[idx].impulse_magnitude = impulse;
    world->impact_event_count++;
}

/* ── Tests ──────────────────────────────────────────────────────── */

/**
 * Test 1: A freshly initialized world has zero impact events.
 */
static int test_impact_events_empty_initially(void)
{
    phys_world_t world;
    phys_world_config_t cfg = test_config();
    ASSERT_TRUE(phys_world_init(&world, &cfg) == 0);

    uint32_t count = 99;
    const phys_impact_event_t *events =
        phys_world_get_impact_events(&world, &count);

    ASSERT_TRUE(events != NULL);
    ASSERT_TRUE(count == 0);

    phys_world_destroy(&world);
    return 0;
}

/**
 * Test 2: Manually add events to the buffer and retrieve them.
 */
static int test_impact_events_add_and_retrieve(void)
{
    phys_world_t world;
    phys_world_config_t cfg = test_config();
    ASSERT_TRUE(phys_world_init(&world, &cfg) == 0);

    push_event(&world, 1, 2, 10.0f);
    push_event(&world, 3, 4, 20.0f);

    uint32_t count = 0;
    const phys_impact_event_t *events =
        phys_world_get_impact_events(&world, &count);

    ASSERT_TRUE(count == 2);
    ASSERT_TRUE(events[0].body_a == 1);
    ASSERT_TRUE(events[0].body_b == 2);
    ASSERT_FLOAT_NEAR(10.0f, events[0].impulse_magnitude, 1e-5f);
    ASSERT_TRUE(events[1].body_a == 3);
    ASSERT_TRUE(events[1].body_b == 4);
    ASSERT_FLOAT_NEAR(20.0f, events[1].impulse_magnitude, 1e-5f);

    phys_world_destroy(&world);
    return 0;
}

/**
 * Test 3: Clear resets event count to zero.
 */
static int test_impact_events_clear(void)
{
    phys_world_t world;
    phys_world_config_t cfg = test_config();
    ASSERT_TRUE(phys_world_init(&world, &cfg) == 0);

    push_event(&world, 1, 2, 5.0f);
    push_event(&world, 3, 4, 15.0f);

    phys_world_clear_impact_events(&world);

    uint32_t count = 99;
    phys_world_get_impact_events(&world, &count);
    ASSERT_TRUE(count == 0);

    phys_world_destroy(&world);
    return 0;
}

/**
 * Test 4: Filter events for a specific body index.
 */
static int test_impact_events_for_body(void)
{
    phys_world_t world;
    phys_world_config_t cfg = test_config();
    ASSERT_TRUE(phys_world_init(&world, &cfg) == 0);

    /* body 1 involved in events 0 and 2, body 3 in event 1. */
    push_event(&world, 1, 2, 10.0f);
    push_event(&world, 3, 4, 20.0f);
    push_event(&world, 5, 1, 30.0f);

    phys_impact_event_t out[8];
    uint32_t found = phys_world_get_impact_events_for_body(
        &world, 1, out, 8);

    ASSERT_TRUE(found == 2);
    /* First match: body_a==1. */
    ASSERT_TRUE(out[0].body_a == 1);
    ASSERT_TRUE(out[0].body_b == 2);
    ASSERT_FLOAT_NEAR(10.0f, out[0].impulse_magnitude, 1e-5f);
    /* Second match: body_b==1. */
    ASSERT_TRUE(out[1].body_a == 5);
    ASSERT_TRUE(out[1].body_b == 1);
    ASSERT_FLOAT_NEAR(30.0f, out[1].impulse_magnitude, 1e-5f);

    /* Filter for body 3: should get 1 result. */
    found = phys_world_get_impact_events_for_body(&world, 3, out, 8);
    ASSERT_TRUE(found == 1);
    ASSERT_TRUE(out[0].body_a == 3);

    /* Filter for body 99: should get 0 results. */
    found = phys_world_get_impact_events_for_body(&world, 99, out, 8);
    ASSERT_TRUE(found == 0);

    phys_world_destroy(&world);
    return 0;
}

/**
 * Test 5: get_strongest returns the event with highest impulse for a body.
 */
static int test_impact_strongest(void)
{
    phys_world_t world;
    phys_world_config_t cfg = test_config();
    ASSERT_TRUE(phys_world_init(&world, &cfg) == 0);

    push_event(&world, 1, 2, 10.0f);
    push_event(&world, 1, 3, 50.0f);  /* strongest for body 1 */
    push_event(&world, 1, 4, 25.0f);
    push_event(&world, 5, 6, 100.0f); /* unrelated */

    phys_impact_event_t strongest;
    bool found = phys_world_get_strongest_impact(&world, 1, &strongest);
    ASSERT_TRUE(found);
    ASSERT_TRUE(strongest.body_a == 1);
    ASSERT_TRUE(strongest.body_b == 3);
    ASSERT_FLOAT_NEAR(50.0f, strongest.impulse_magnitude, 1e-5f);

    /* Body with no events returns false. */
    found = phys_world_get_strongest_impact(&world, 99, &strongest);
    ASSERT_TRUE(!found);

    phys_world_destroy(&world);
    return 0;
}

/**
 * Test 6: set/get impact threshold.
 */
static int test_impact_threshold(void)
{
    phys_world_t world;
    phys_world_config_t cfg = test_config();
    ASSERT_TRUE(phys_world_init(&world, &cfg) == 0);

    /* Default threshold should be 1.0f. */
    ASSERT_FLOAT_NEAR(1.0f, phys_world_get_impact_threshold(&world), 1e-5f);

    phys_world_set_impact_threshold(&world, 42.5f);
    ASSERT_FLOAT_NEAR(42.5f, phys_world_get_impact_threshold(&world), 1e-5f);

    /* Negative threshold is clamped to 0. */
    phys_world_set_impact_threshold(&world, -5.0f);
    ASSERT_FLOAT_NEAR(0.0f, phys_world_get_impact_threshold(&world), 1e-5f);

    phys_world_destroy(&world);
    return 0;
}

/* ── Main ───────────────────────────────────────────────────────── */

int main(void)
{
    int test_count = 0;
    int fail_count = 0;

    printf("p050_physics_impact_event_tests:\n");

    RUN_TEST(test_impact_events_empty_initially);
    RUN_TEST(test_impact_events_add_and_retrieve);
    RUN_TEST(test_impact_events_clear);
    RUN_TEST(test_impact_events_for_body);
    RUN_TEST(test_impact_strongest);
    RUN_TEST(test_impact_threshold);

    printf("\n%d/%d tests passed\n", test_count - fail_count, test_count);
    return fail_count ? 1 : 0;
}
