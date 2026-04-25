/**
 * @file llm_cost_tracker_tests.c
 * @brief Tests for LLM cost tracker (atomic math).
 *
 * Phase 1 (RED): Tests compile but fail until implementation lands.
 * Covers: init, add, get, compute with various token counts.
 */

#include <stdio.h>
#include <math.h>
#include <string.h>

#include "ferrum/llm/llm_cost_tracker.h"

/* ── Test harness ──────────────────────────────────────────────── */

static int g_pass = 0;
static int g_fail = 0;

#define RUN(fn)                                                        \
    do {                                                                 \
        printf("RUN  %s\n", #fn);                                       \
        fn();                                                            \
        printf("OK   %s\n", #fn);                                       \
    } while (0)

#define ASSERT_TRUE(expr)                                              \
    do {                                                                 \
        if (!(expr)) {                                                   \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr);       \
            g_fail++;                                                    \
            return;                                                      \
        }                                                                \
    } while (0)

#define ASSERT_EQ(a, b)   ASSERT_TRUE((a) == (b))
#define ASSERT_NEAR(a, b, eps) ASSERT_TRUE(fabsf((float)(a) - (float)(b)) < (eps))

#define PASS() g_pass++

/* ── Tests ─────────────────────────────────────────────────────── */

static void test_init_zero(void) {
    llm_cost_tracker_t ct;
    llm_cost_tracker_init(&ct);
    ASSERT_NEAR(llm_cost_tracker_get(&ct), 0.0f, 1e-6f);
    PASS();
}

static void test_add_increases_total(void) {
    llm_cost_tracker_t ct;
    llm_cost_tracker_init(&ct);
    float total = llm_cost_tracker_add(&ct, 0.05f);
    ASSERT_NEAR(total, 0.05f, 1e-6f);
    ASSERT_NEAR(llm_cost_tracker_get(&ct), 0.05f, 1e-6f);
    PASS();
}

static void test_add_accumulates(void) {
    llm_cost_tracker_t ct;
    llm_cost_tracker_init(&ct);
    llm_cost_tracker_add(&ct, 0.01f);
    llm_cost_tracker_add(&ct, 0.02f);
    llm_cost_tracker_add(&ct, 0.03f);
    ASSERT_NEAR(llm_cost_tracker_get(&ct), 0.06f, 1e-6f);
    PASS();
}

static void test_compute_simple(void) {
    /* $0.001 per 1K input, $0.003 per 1K output. */
    float cost = llm_cost_compute(1000, 1000, 0.001f, 0.003f);
    ASSERT_NEAR(cost, 0.004f, 1e-6f);
    PASS();
}

static void test_compute_zero_tokens(void) {
    float cost = llm_cost_compute(0, 0, 0.001f, 0.003f);
    ASSERT_NEAR(cost, 0.0f, 1e-6f);
    PASS();
}

static void test_compute_partial_tokens(void) {
    /* 500 input + 250 output → half rates. */
    float cost = llm_cost_compute(500, 250, 0.001f, 0.003f);
    ASSERT_NEAR(cost, 0.00125f, 1e-6f);
    PASS();
}

static void test_compute_large_numbers(void) {
    /* 1M input + 500K output. */
    float cost = llm_cost_compute(1000000, 500000, 0.001f, 0.003f);
    ASSERT_NEAR(cost, 2.5f, 1e-4f);
    PASS();
}

static void test_add_returns_new_total(void) {
    llm_cost_tracker_t ct;
    llm_cost_tracker_init(&ct);
    float t1 = llm_cost_tracker_add(&ct, 0.1f);
    float t2 = llm_cost_tracker_add(&ct, 0.2f);
    ASSERT_NEAR(t1, 0.1f, 1e-6f);
    ASSERT_NEAR(t2, 0.3f, 1e-6f);
    PASS();
}

/* ── Main ──────────────────────────────────────────────────────── */

int main(void) {
    printf("=== LLM Cost Tracker Tests ===\n\n");

    RUN(test_init_zero);
    RUN(test_add_increases_total);
    RUN(test_add_accumulates);
    RUN(test_compute_simple);
    RUN(test_compute_zero_tokens);
    RUN(test_compute_partial_tokens);
    RUN(test_compute_large_numbers);
    RUN(test_add_returns_new_total);

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
