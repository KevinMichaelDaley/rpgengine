#include <cstdio>
#include <cmath>
#include <cstdlib>

#include <symx>
#include "ferrum/procgen/srd/srd_energy.h"

using srd::srd_corridor_sdf_energy;

static int g_pass = 0;
static int g_fail = 0;

#define RUN(fn) do { printf("RUN  %s\n", #fn); fn(); printf("OK   %s\n\n", #fn); } while (0)
#define ASSERT_TRUE(expr) do { if (!(expr)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); g_fail++; return; } } while (0)
#define ASSERT_LT(a, b) ASSERT_TRUE((a) < (b))
#define ASSERT_GT(a, b) ASSERT_TRUE((a) > (b))
#define ASSERT_NEAR(a, b, eps) ASSERT_TRUE(fabs((a)-(b)) <= (eps))
#define PASS() g_pass++

static double corridor_sdf(double x, double y, double z,
                           double fx, double fz, double tx, double tz,
                           double radius, double floor_y, double ceil_y) {
    double dx = tx - fx, dz = tz - fz;
    double len2 = dx*dx + dz*dz;
    double t = ((x-fx)*dx + (z-fz)*dz) / len2;
    if (t < 0) t = 0; if (t > 1) t = 1;
    double cx = fx + t*dx, cz = fz + t*dz;
    double d_2d = sqrt((x-cx)*(x-cx) + (z-cz)*(z-cz)) - radius;
    double d_y = fmax(floor_y - y, y - ceil_y);
    return fmax(d_2d, d_y);
}

static void test_corridor_sdf_center_inside(void) {
    /* Point on centerline of corridor: negative → inside */
    double s = corridor_sdf(7.5, 2.0, 2.0,  5.0,2.0, 10.0,2.0,  1.5, 0.0,4.0);
    ASSERT_LT(s, 0.0);
    PASS();
}

static void test_corridor_sdf_endpoint(void) {
    /* Point at exact endpoint on centerline: sdf = -radius */
    double s = corridor_sdf(5.0, 2.0, 2.0,  5.0,2.0, 10.0,2.0,  1.5, 0.0,4.0);
    ASSERT_NEAR(s, -1.5, 1e-4);  /* inside by radius */
    PASS();
}

static void test_corridor_sdf_outside(void) {
    /* Far point: positive sdf */
    double s = corridor_sdf(50.0, 2.0, 2.0,  5.0,2.0, 10.0,2.0,  1.5, 0.0,4.0);
    ASSERT_GT(s, 30.0);
    PASS();
}

static void test_corridor_sdf_outside_vertically(void) {
    /* Point above ceiling: positive */
    double s = corridor_sdf(7.5, 10.0, 2.0,  5.0,2.0, 10.0,2.0,  1.5, 0.0,4.0);
    ASSERT_GT(s, 5.0);
    PASS();
}

static void test_corridor_energy_symx(void) {
    symx::Workspace ws;
    symx::Scalar fx = ws.make_scalar(), fz = ws.make_scalar();
    symx::Scalar tx = ws.make_scalar(), tz = ws.make_scalar();
    symx::Scalar r  = ws.make_scalar();
    symx::Scalar fY = ws.make_scalar(), cY = ws.make_scalar();

    symx::Scalar energy = srd_corridor_sdf_energy(ws, fx, fz, tx, tz, r, fY, cY, 0.5);

    symx::Compiled<double> c({ energy }, "corr_energy", "../codegen_srd");
    c.set(fx, 5.0); c.set(fz, 2.0);
    c.set(tx, 10.0); c.set(tz, 2.0);
    c.set(r, 1.5);
    c.set(fY, 0.0); c.set(cY, 4.0);

    symx::View<double> result = c.run();
    ASSERT_GT(result[0], 0.0);
    ASSERT_LT(result[0], 0.5);
    PASS();
}

int main(void) {
    printf("=== Corridor SDF Energy Tests ===\n\n");

    RUN(test_corridor_sdf_center_inside);
    RUN(test_corridor_sdf_endpoint);
    RUN(test_corridor_sdf_outside);
    RUN(test_corridor_sdf_outside_vertically);
    RUN(test_corridor_energy_symx);

    printf("=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
