#include <cstdio>
#include <cmath>
#include <cstdlib>

#include <symx>

static int g_pass = 0;
static int g_fail = 0;

#define RUN(fn) do { printf("RUN  %s\n", #fn); fn(); printf("OK   %s\n\n", #fn); } while (0)
#define ASSERT_TRUE(expr) do { if (!(expr)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); g_fail++; return; } } while (0)
#define ASSERT_LT(a, b) ASSERT_TRUE((a) < (b))
#define ASSERT_GT(a, b) ASSERT_TRUE((a) > (b))
#define PASS() g_pass++

// Simple: energy = (cx - target)^2 + (cz - target)^2
// Room at target → energy 0; shifted → energy > 0
static symx::Scalar room_dist_energy(symx::Workspace &ws,
                                      symx::Scalar cx, symx::Scalar cz,
                                      double target_cx, double target_cz) {
    using Scalar = symx::Scalar;
    Scalar tx = cx.make_constant(target_cx);
    Scalar tz = cx.make_constant(target_cz);
    Scalar e = (cx - tx) * (cx - tx) + (cz - tz) * (cz - tz);
    return e;
}

static void test_symx_room_distance_energy(void) {
    symx::Workspace ws;
    symx::Scalar cx = ws.make_scalar();
    symx::Scalar cz = ws.make_scalar();

    symx::Scalar e_on_target  = room_dist_energy(ws, cx, cz, 10.0, 5.0);
    symx::Scalar e_shifted    = room_dist_energy(ws, cx, cz, 50.0, 50.0);

    symx::Compiled<double> compiled({ e_on_target, e_shifted },
                                    "room_dist", "../codegen_srd");
    compiled.set(cx, 10.0);
    compiled.set(cz, 5.0);

    symx::View<double> result = compiled.run();
    double et = result[0];  // (10-10)^2 + (5-5)^2 = 0
    double es = result[1];  // (10-50)^2 + (5-50)^2 = 1600+2025 = 3625
    ASSERT_LT(et, 1e-6);
    ASSERT_GT(es, 1.0);
    PASS();
}

// SDF-based room energy: sample one point at center, compute occ error
static symx::Scalar room_sdf_energy(symx::Workspace &ws,
                                     symx::Scalar cx, symx::Scalar cz,
                                     symx::Scalar hx, symx::Scalar hy, symx::Scalar hz,
                                     double temperature) {
    using Scalar = symx::Scalar;
    Scalar zero = cx.get_zero();
    Scalar one  = cx.get_one();
    Scalar temp = cx.make_constant(temperature);

    /* Sample room center point */
    Scalar dx = symx::abs(cx - cx) - hx;   // = -hx (negative → inside)
    Scalar dy = symx::abs(hy * 0.0f) - hy;  // = -hy
    Scalar dz = symx::abs(cz - cz) - hz;   // = -hz

    Scalar outside = symx::max(dx, symx::max(dy, dz));
    outside = symx::max(outside, zero);

    Scalar inside = symx::min(dx, symx::min(dy, dz));
    inside = symx::min(inside, zero);

    Scalar sdf = outside + inside;
    Scalar occ = one / (one + symx::exp(sdf / temp));
    Scalar err = occ - one;
    return err * err;
}

static void test_symx_room_sdf_energy(void) {
    symx::Workspace ws;
    symx::Scalar cx = ws.make_scalar();
    symx::Scalar cz = ws.make_scalar();
    symx::Scalar hx = ws.make_scalar();
    symx::Scalar hy = ws.make_scalar();
    symx::Scalar hz = ws.make_scalar();

    symx::Scalar energy = room_sdf_energy(ws, cx, cz, hx, hy, hz, 0.5);

    symx::Compiled<double> compiled({ energy }, "room_sdf", "../codegen_srd");
    compiled.set(cx, 10.0);
    compiled.set(cz, 5.0);
    compiled.set(hx, 4.0);
    compiled.set(hy, 3.0);
    compiled.set(hz, 3.0);

    symx::View<double> result = compiled.run();
    ASSERT_GT(result[0], 0.0);
    ASSERT_LT(result[0], 0.5);   // center point: sdf < 0 → occ ≈ 1 → err ≈ 0
    PASS();
}

int main(void) {
    printf("=== SRD Energy Tests ===\n\n");

    RUN(test_symx_room_distance_energy);
    RUN(test_symx_room_sdf_energy);

    printf("=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
