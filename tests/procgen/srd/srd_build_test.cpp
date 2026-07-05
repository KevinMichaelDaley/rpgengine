#include <symx>

#include <cstdio>
#include <cmath>
#include <cstdlib>

static int g_pass = 0;
static int g_fail = 0;

#define RUN(fn) do { printf("RUN  %s\n", #fn); fn(); printf("OK   %s\n\n", #fn); } while (0)
#define ASSERT_TRUE(expr) do { if (!(expr)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); g_fail++; return; } } while (0)
#define ASSERT_FLOAT_EQ(a, b, eps) do { if (fabs((a) - (b)) > (eps)) { printf("FAIL %s:%d: |%f - %f| > %f\n", __FILE__, __LINE__, (double)(a), (double)(b), (double)(eps)); g_fail++; return; } } while (0)

static void test_symx_scalar_create(void) {
    symx::Workspace ws;
    symx::Scalar x = ws.make_scalar();

    (void)x;
    g_pass++;
}

static void test_symx_expression_compile(void) {
    symx::Workspace ws;
    symx::Scalar x = ws.make_scalar();
    symx::Scalar y = x + 2.0;

    symx::Compiled<double> compiled({ y }, "smoke_add", "../codegen_srd");
    compiled.set(x, 5.0);

    symx::View<double> result = compiled.run();
    ASSERT_FLOAT_EQ(result[0], 7.0, 1e-6);
    g_pass++;
}

static void test_symx_derivative(void) {
    symx::Workspace ws;
    symx::Scalar x = ws.make_scalar();
    symx::Scalar f = symx::sin(x) * x;

    symx::Scalar df = symx::diff(f, x);

    symx::Compiled<double> compiled({ df }, "smoke_diff", "../codegen_srd");
    compiled.set(x, 0.0);

    symx::View<double> result = compiled.run();
    ASSERT_FLOAT_EQ(result[0], 0.0, 1e-6);
    g_pass++;
}

int main(void) {
    printf("=== SRD Build Smoke Test ===\n\n");

    RUN(test_symx_scalar_create);
    RUN(test_symx_expression_compile);
    RUN(test_symx_derivative);

    printf("=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
