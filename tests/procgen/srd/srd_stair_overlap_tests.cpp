#include <cstdio>
#include <cmath>
#include <cstdlib>

#include <symx>
#include "ferrum/procgen/srd/srd_energy.h"

using srd::srd_stair_alignment_energy;
using srd::srd_overlap_energy;

static int g_pass = 0;
static int g_fail = 0;

#define RUN(fn) do { printf("RUN  %s\n", #fn); fn(); printf("OK   %s\n\n", #fn); } while (0)
#define ASSERT_TRUE(expr) do { if (!(expr)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); g_fail++; return; } } while (0)
#define ASSERT_LT(a, b) ASSERT_TRUE((a) < (b))
#define ASSERT_GT(a, b) ASSERT_TRUE((a) > (b))
#define ASSERT_NEAR(a, b, eps) ASSERT_TRUE(fabs((a)-(b)) <= (eps))
#define PASS() g_pass++

static void test_stair_energy_zero_when_aligned(void) {
    symx::Workspace ws;
    symx::Scalar ax = ws.make_scalar(), az = ws.make_scalar();
    symx::Scalar tx = ws.make_scalar(), tz = ws.make_scalar();

    symx::Scalar energy = srd_stair_alignment_energy(ws, ax, az, tx, tz);

    symx::Compiled<double> c({ energy }, "stair_zero", "../codegen_srd");
    c.set(ax, 5.0); c.set(az, 3.0);
    c.set(tx, 5.0); c.set(tz, 3.0);  /* aligned */
    symx::View<double> r = c.run();
    ASSERT_NEAR(r[0], 0.0, 1e-6);
    PASS();
}

static void test_stair_energy_increases_with_offset(void) {
    symx::Workspace ws;
    symx::Scalar ax = ws.make_scalar(), az = ws.make_scalar();
    symx::Scalar tx = ws.make_scalar(), tz = ws.make_scalar();

    symx::Scalar e_aligned = srd_stair_alignment_energy(ws, ax, az, tx, tz);
    symx::Scalar e_offset  = srd_stair_alignment_energy(ws, ax + 5.0, az + 5.0, tx, tz);

    symx::Compiled<double> c({ e_aligned, e_offset }, "stair_off", "../codegen_srd");
    c.set(ax, 5.0); c.set(az, 3.0);
    c.set(tx, 5.0); c.set(tz, 3.0);
    symx::View<double> r = c.run();
    ASSERT_LT(r[0], r[1]);
    PASS();
}

static void test_overlap_zero_when_far_apart(void) {
    /* Two rooms far apart → Gaussian overlap ≈ 0 */
    symx::Workspace ws;
    symx::Scalar ax = ws.make_scalar(), az = ws.make_scalar();
    symx::Scalar ahx = ws.make_scalar(), ahy = ws.make_scalar(), ahz = ws.make_scalar();
    symx::Scalar bx = ws.make_scalar(), bz = ws.make_scalar();
    symx::Scalar bhx = ws.make_scalar(), bhy = ws.make_scalar(), bhz = ws.make_scalar();

    symx::Scalar e = srd_overlap_energy(ws,
        ax, az, ahx, ahy, ahz,
        bx, bz, bhx, bhy, bhz,
        0.5);

    symx::Compiled<double> c({ e }, "overlap_far", "../codegen_srd");
    c.set(ax, 0.0); c.set(az, 0.0); c.set(ahx, 2.0); c.set(ahy, 2.0); c.set(ahz, 2.0);
    c.set(bx, 50.0); c.set(bz, 50.0); c.set(bhx, 2.0); c.set(bhy, 2.0); c.set(bhz, 2.0);
    symx::View<double> r = c.run();
    ASSERT_NEAR(r[0], 0.0, 1e-3);  /* exp(-50²/(4²)) ≈ exp(-156) ≈ 0 */
    PASS();
}

static void test_overlap_positive_when_overlapping(void) {
    /* Two identical rooms at same position → distance=0 → exp(0)=1 */
    symx::Workspace ws;
    symx::Scalar ax = ws.make_scalar(), az = ws.make_scalar();
    symx::Scalar ahx = ws.make_scalar(), ahy = ws.make_scalar(), ahz = ws.make_scalar();
    symx::Scalar bx = ws.make_scalar(), bz = ws.make_scalar();
    symx::Scalar bhx = ws.make_scalar(), bhy = ws.make_scalar(), bhz = ws.make_scalar();

    symx::Scalar e = srd_overlap_energy(ws,
        ax, az, ahx, ahy, ahz,
        bx, bz, bhx, bhy, bhz,
        0.5);

    symx::Compiled<double> c({ e }, "overlap_on", "../codegen_srd");
    c.set(ax, 10.0); c.set(az, 5.0); c.set(ahx, 4.0); c.set(ahy, 3.0); c.set(ahz, 3.0);
    c.set(bx, 10.0); c.set(bz, 5.0); c.set(bhx, 4.0); c.set(bhy, 3.0); c.set(bhz, 3.0);
    symx::View<double> r = c.run();
    ASSERT_NEAR(r[0], 1.0, 1e-3);  /* identical centers → dist=0 → exp(0)=1 */
    PASS();
}

int main(void) {
    printf("=== Stair + Overlap Energy Tests ===\n\n");

    RUN(test_stair_energy_zero_when_aligned);
    RUN(test_stair_energy_increases_with_offset);
    RUN(test_overlap_zero_when_far_apart);
    RUN(test_overlap_positive_when_overlapping);

    printf("=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
