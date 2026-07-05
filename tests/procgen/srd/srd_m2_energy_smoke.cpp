#include <cstdio>
#include <cmath>
#include <cstdlib>

#include <symx>
#include "ferrum/procgen/srd/srd_energy.h"

using srd::srd_room_sdf_energy;
using srd::srd_corridor_sdf_energy;
using srd::srd_stair_alignment_energy;

static int g_pass = 0;
static int g_fail = 0;

#define RUN(fn) do { printf("RUN  %s\n", #fn); fn(); printf("OK   %s\n\n", #fn); } while (0)
#define ASSERT_TRUE(expr) do { if (!(expr)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); g_fail++; return; } } while (0)
#define ASSERT_LT(a, b) ASSERT_TRUE((a) < (b))
#define ASSERT_GT(a, b) ASSERT_TRUE((a) > (b))
#define PASS() g_pass++

static void test_composite_scene_energy(void) {
    /* Evaluate each component separately and sum in C++ */
    double e_total = 0.0;

    /* Room */
    {
        symx::Workspace ws;
        symx::Scalar cx = ws.make_scalar(), cz = ws.make_scalar();
        symx::Scalar hx = ws.make_scalar(), hy = ws.make_scalar(), hz = ws.make_scalar();
        symx::Scalar E = srd_room_sdf_energy(ws, cx, cz, hx, hy, hz, 0.5);
        symx::Compiled<double> c({ E }, "m2_room", "../codegen_srd");
        c.set(cx, 0.0); c.set(cz, 0.0);
        c.set(hx, 4.0); c.set(hy, 3.0); c.set(hz, 4.0);
        e_total += c.run()[0];
    }

    /* Corridor */
    {
        symx::Workspace ws;
        symx::Scalar fx = ws.make_scalar(), fz = ws.make_scalar();
        symx::Scalar tx = ws.make_scalar(), tz = ws.make_scalar();
        symx::Scalar r  = ws.make_scalar(), fy = ws.make_scalar(), cy = ws.make_scalar();
        symx::Scalar E = srd_corridor_sdf_energy(ws, fx, fz, tx, tz, r, fy, cy, 0.5);
        symx::Compiled<double> c({ E }, "m2_corr", "../codegen_srd");
        c.set(fx, 4.0); c.set(fz, 0.0);
        c.set(tx, 6.0); c.set(tz, 0.0);
        c.set(r, 1.5); c.set(fy, 0.0); c.set(cy, 4.0);
        e_total += c.run()[0];
    }

    /* Stair */
    {
        symx::Workspace ws;
        symx::Scalar ax = ws.make_scalar(), az = ws.make_scalar();
        symx::Scalar tx = ws.make_scalar(), tz = ws.make_scalar();
        symx::Scalar E = srd_stair_alignment_energy(ws, ax, az, tx, tz);
        symx::Compiled<double> c({ E }, "m2_stair", "../codegen_srd");
        c.set(ax, 5.0); c.set(az, 2.0);
        c.set(tx, 5.0); c.set(tz, 2.0);
        e_total += c.run()[0];
    }

    ASSERT_GT(e_total, 0.0);
    ASSERT_LT(e_total, 10.0);
    PASS();
}

int main(void) {
    printf("=== M2: Energy Elements Integration ===\n\n");

    RUN(test_composite_scene_energy);

    printf("=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
