#include <cstdio>
#include <cmath>
#include <cstring>

#include "ferrum/procgen/srd/srd_eikonal.h"
#include "ferrum/procgen/srd/srd_transport.h"

static int g_pass = 0;
static int g_fail = 0;

#define RUN(fn) do { printf("RUN  %s\n", #fn); fn(); printf("OK   %s\n\n", #fn); } while (0)
#define ASSERT_TRUE(expr) do { if (!(expr)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); g_fail++; return; } } while (0)
#define ASSERT_LT(a, b) ASSERT_TRUE((a) < (b))
#define ASSERT_GT(a, b) ASSERT_TRUE((a) > (b))
#define ASSERT_NEAR(a, b, eps) ASSERT_TRUE(fabs((a)-(b)) <= (eps))
#define PASS() g_pass++

static void test_eikonal_empty_grid(void) {
    double occ[45]; memset(occ, 0, sizeof(occ));
    double T[45];
    srd_eikonal_solve_2d(9, 5, occ, 1, 2, T);
    double t_target = T[2 * 9 + 6];  /* (6,2) */
    ASSERT_NEAR(t_target, 5.0, 0.5);
    PASS();
}

static void test_eikonal_with_wall(void) {
    double occ[45]; memset(occ, 0, sizeof(occ));
    occ[2 * 9 + 3] = 1.0;
    occ[2 * 9 + 4] = 1.0;
    double T[45];
    srd_eikonal_solve_2d(9, 5, occ, 1, 2, T);
    double t_target = T[2 * 9 + 6];
    ASSERT_GT(t_target, 5.5);
    PASS();
}

static void test_transport_empty(void) {
    double occ[45]; memset(occ, 0, sizeof(occ));
    double R[45];
    double r = srd_transport_solve_2d(9, 5, occ, 1, 2, 7, 2, R);
    ASSERT_GT(r, 0.1);
    PASS();
}

static void test_transport_occluded(void) {
    double occ[45]; memset(occ, 0, sizeof(occ));
    occ[2 * 9 + 4] = 1.0;
    double R[45];
    double r_wall = srd_transport_solve_2d(9, 5, occ, 1, 2, 7, 2, R);

    memset(occ, 0, sizeof(occ));
    double r_clear = srd_transport_solve_2d(9, 5, occ, 1, 2, 7, 2, R);
    ASSERT_LT(r_wall, r_clear);
    PASS();
}

int main(void) {
    printf("=== PDE Solver Tests ===\n\n");

    RUN(test_eikonal_empty_grid);
    RUN(test_eikonal_with_wall);
    RUN(test_transport_empty);
    RUN(test_transport_occluded);

    printf("=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
