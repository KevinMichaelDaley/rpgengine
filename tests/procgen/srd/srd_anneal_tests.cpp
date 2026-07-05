#include <stdio.h>
#include <math.h>

#include "ferrum/procgen/srd/srd_anneal.h"

static int g_pass = 0;
static int g_fail = 0;

#define RUN(fn) do { printf("RUN  %s\n", #fn); fn(); printf("OK   %s\n\n", #fn); } while (0)
#define ASSERT_TRUE(expr) do { if (!(expr)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); g_fail++; return; } } while (0)
#define ASSERT_FLOAT_EQ(a, b, eps) ASSERT_TRUE(fabs((a)-(b)) <= (eps))
#define PASS() g_pass++

static void test_temp_starts_at_init(void) {
    srd_anneal_t a;
    srd_anneal_init(&a, 0.5, 0.995, 0.01);
    ASSERT_FLOAT_EQ(srd_anneal_current(&a), 0.5, 1e-6);
    PASS();
}

static void test_temp_decays_each_step(void) {
    srd_anneal_t a;
    srd_anneal_init(&a, 0.5, 0.995, 0.01);
    for (int i = 0; i < 100; i++) srd_anneal_step(&a);
    double expected = 0.5 * pow(0.995, 100);
    ASSERT_FLOAT_EQ(srd_anneal_current(&a), expected, 1e-6);
    PASS();
}

static void test_temp_floors_at_min(void) {
    srd_anneal_t a;
    srd_anneal_init(&a, 0.5, 0.9, 0.1);  /* fast decay */
    for (int i = 0; i < 500; i++) srd_anneal_step(&a);
    ASSERT_FLOAT_EQ(srd_anneal_current(&a), 0.1, 1e-6);
    PASS();
}

int main(void) {
    printf("=== Anneal Tests ===\n\n");

    RUN(test_temp_starts_at_init);
    RUN(test_temp_decays_each_step);
    RUN(test_temp_floors_at_min);

    printf("=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
