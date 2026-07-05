#include <cstdio>
#include <cmath>
#include <cstdlib>

#include "ferrum/procgen/procgen_srd_types.h"

extern void srd_eikonal_gradient(const fr_room_box_t *rooms, uint32_t n,
                                  uint32_t src, uint32_t tgt,
                                  int nx, int nz,
                                  double *gx, double *gz, uint32_t nr);

static int g_pass = 0;
static int g_fail = 0;

#define RUN(fn) do { printf("RUN  %s\n", #fn); fn(); printf("OK   %s\n\n", #fn); } while (0)
#define ASSERT_TRUE(expr) do { if (!(expr)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); g_fail++; return; } } while (0)
#define ASSERT_GT(a, b) ASSERT_TRUE((a) > (b))
#define PASS() g_pass++

static fr_room_box_t rr(float cx, float cz, float hx, float hz) {
    fr_room_box_t b; fr_room_box_init(&b);
    b.center_x=cx; b.center_z=cz; b.half_extent_x=hx; b.half_extent_z=hz;
    b.floor_z=0; b.ceil_z=4; return b;
}

/* Encode grid (x,z) → linear index: src_idx = z*nx + x */
static uint32_t gi(int x, int z, int nx) { return (uint32_t)(z*nx + x); }

static void test_eikonal_gradient_obstacle(void) {
    /* 40x20 grid, wall at center blocks direct path from left to right.
       Source at grid (8,10), target at (32,10).
       Gradient on wall should push it up or down to shorten path. */
    fr_room_box_t wall = rr(0, 0, 10, 1); /* wall at origin, 20x2m */
    int nx=40, nz=20;

    double gx=0, gz=0;
    srd_eikonal_gradient(&wall, 1,
                          gi(8, 10, nx), gi(32, 10, nx),
                          nx, nz, &gx, &gz, 1);

    /* Loss = T(target). Gradient should push wall away from the
       source-target line (z=0). Expect non-zero gradient in z. */
    ASSERT_GT(fabs(gz), 0.0);
    PASS();
}

static void test_eikonal_gradient_moves_obstacle(void) {
    /* Wall blocks source (8,10) → target (32,10).
       Gradient on wall center should push it to one side. */
    fr_room_box_t wall = rr(0, 0, 4, 3);
    int nx=40, nz=20;
    double gx=0, gz=0;
    srd_eikonal_gradient(&wall, 1,
                          gi(8,10,nx), gi(32,10,nx),
                          nx, nz, &gx, &gz, 1);
    ASSERT_GT(fabs(gx)+fabs(gz), 0.0);
    PASS();
}

int main(void) {
    printf("=== PDE Gradient Tests ===\n\n");
    RUN(test_eikonal_gradient_obstacle);
    RUN(test_eikonal_gradient_moves_obstacle);
    printf("=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
