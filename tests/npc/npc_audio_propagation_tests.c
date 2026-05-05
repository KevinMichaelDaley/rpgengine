/**
 * @file npc_audio_propagation_tests.c
 * @brief Audio propagation graph tests.
 *
 * Covers:
 * - Material presets have correct default values
 * - Distance query returns higher attenuation for longer distance
 * - Inaudible threshold works
 * - Simple create/destroy
 */

#include "ferrum/npc/npc_audio_propagation.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define RUN(fn) do { printf("  %-48s ", #fn); fn(); } while (0)
#define ASSERT_TRUE(expr) do { \
    if (!(expr)) { \
        printf("FAIL (%s:%d)\n", __FILE__, __LINE__); \
        g_fail++; \
        return; \
    } \
} while (0)
#define ASSERT_FLOAT_NEAR(exp, act, tol) do { \
    if (fabsf((exp) - (act)) > (tol)) { \
        printf("FAIL (%s:%d) expected %.6f got %.6f\n", \
               __FILE__, __LINE__, (float)(exp), (float)(act)); \
        g_fail++; \
        return; \
    } \
} while (0)
#define PASS() do { printf("PASS\n"); g_pass++; } while (0)

static int g_pass = 0;
static int g_fail = 0;

/* ── Material preset tests ──────────────────────────────────────── */

static void test_material_wood_preset(void) {
    npc_acoustic_material_t m = (npc_acoustic_material_t)NPC_ACOUSTIC_MATERIAL_WOOD;
    ASSERT_FLOAT_NEAR(0.11f, m.absorption_low,  0.001f);
    ASSERT_FLOAT_NEAR(0.10f, m.absorption_mid,  0.001f);
    ASSERT_FLOAT_NEAR(0.07f, m.absorption_high, 0.001f);
    ASSERT_FLOAT_NEAR(0.15f, m.scattering,      0.001f);
    ASSERT_FLOAT_NEAR(0.03f, m.transmission,    0.001f);
    PASS();
}

static void test_material_stone_preset(void) {
    npc_acoustic_material_t m = (npc_acoustic_material_t)NPC_ACOUSTIC_MATERIAL_STONE;
    ASSERT_FLOAT_NEAR(0.01f, m.absorption_low,  0.001f);
    ASSERT_FLOAT_NEAR(0.01f, m.absorption_mid,  0.001f);
    ASSERT_FLOAT_NEAR(0.01f, m.absorption_high, 0.001f);
    ASSERT_FLOAT_NEAR(0.05f, m.scattering,      0.001f);
    ASSERT_FLOAT_NEAR(0.00f, m.transmission,    0.001f);
    PASS();
}

static void test_material_metal_preset(void) {
    npc_acoustic_material_t m = (npc_acoustic_material_t)NPC_ACOUSTIC_MATERIAL_METAL;
    ASSERT_FLOAT_NEAR(0.01f, m.absorption_low,  0.001f);
    ASSERT_FLOAT_NEAR(0.01f, m.absorption_mid,  0.001f);
    ASSERT_FLOAT_NEAR(0.01f, m.absorption_high, 0.001f);
    ASSERT_FLOAT_NEAR(0.05f, m.scattering,      0.001f);
    ASSERT_FLOAT_NEAR(0.00f, m.transmission,    0.001f);
    PASS();
}

static void test_material_glass_preset(void) {
    npc_acoustic_material_t m = (npc_acoustic_material_t)NPC_ACOUSTIC_MATERIAL_GLASS;
    ASSERT_FLOAT_NEAR(0.03f, m.absorption_low,  0.001f);
    ASSERT_FLOAT_NEAR(0.02f, m.absorption_mid,  0.001f);
    ASSERT_FLOAT_NEAR(0.01f, m.absorption_high, 0.001f);
    ASSERT_FLOAT_NEAR(0.05f, m.scattering,      0.001f);
    ASSERT_FLOAT_NEAR(0.10f, m.transmission,    0.001f);
    PASS();
}

static void test_material_snow_preset(void) {
    npc_acoustic_material_t m = (npc_acoustic_material_t)NPC_ACOUSTIC_MATERIAL_SNOW;
    ASSERT_FLOAT_NEAR(0.80f, m.absorption_low,  0.001f);
    ASSERT_FLOAT_NEAR(0.90f, m.absorption_mid,  0.001f);
    ASSERT_FLOAT_NEAR(0.95f, m.absorption_high, 0.001f);
    ASSERT_FLOAT_NEAR(0.20f, m.scattering,      0.001f);
    ASSERT_FLOAT_NEAR(0.00f, m.transmission,    0.001f);
    PASS();
}

static void test_material_ice_preset(void) {
    npc_acoustic_material_t m = (npc_acoustic_material_t)NPC_ACOUSTIC_MATERIAL_ICE;
    ASSERT_FLOAT_NEAR(0.02f, m.absorption_low,  0.001f);
    ASSERT_FLOAT_NEAR(0.02f, m.absorption_mid,  0.001f);
    ASSERT_FLOAT_NEAR(0.02f, m.absorption_high, 0.001f);
    ASSERT_FLOAT_NEAR(0.03f, m.scattering,      0.001f);
    ASSERT_FLOAT_NEAR(0.01f, m.transmission,    0.001f);
    PASS();
}

static void test_material_flesh_preset(void) {
    npc_acoustic_material_t m = (npc_acoustic_material_t)NPC_ACOUSTIC_MATERIAL_FLESH;
    ASSERT_FLOAT_NEAR(0.40f, m.absorption_low,  0.001f);
    ASSERT_FLOAT_NEAR(0.35f, m.absorption_mid,  0.001f);
    ASSERT_FLOAT_NEAR(0.30f, m.absorption_high, 0.001f);
    ASSERT_FLOAT_NEAR(0.10f, m.scattering,      0.001f);
    ASSERT_FLOAT_NEAR(0.05f, m.transmission,    0.001f);
    PASS();
}

static void test_material_air_preset(void) {
    npc_acoustic_material_t m = (npc_acoustic_material_t)NPC_ACOUSTIC_MATERIAL_AIR;
    ASSERT_FLOAT_NEAR(0.00f, m.absorption_low,  0.001f);
    ASSERT_FLOAT_NEAR(0.00f, m.absorption_mid,  0.001f);
    ASSERT_FLOAT_NEAR(0.00f, m.absorption_high, 0.001f);
    ASSERT_FLOAT_NEAR(0.00f, m.scattering,      0.001f);
    ASSERT_FLOAT_NEAR(1.00f, m.transmission,    0.001f);
    PASS();
}

/* ── Query tests ────────────────────────────────────────────────── */

static void test_query_same_point_zero_attenuation(void) {
    npc_audio_graph_t graph;
    npc_audio_graph_init(&graph);

    vec3_t a = {0.0f, 0.0f, 0.0f};
    vec3_t b = {0.0f, 0.0f, 0.0f};
    float atten = npc_audio_graph_query(&graph, a, b);
    ASSERT_FLOAT_NEAR(0.0f, atten, 0.001f);

    npc_audio_graph_destroy(&graph);
    PASS();
}

static void test_query_distance_attenuation_increases(void) {
    npc_audio_graph_t graph;
    npc_audio_graph_init(&graph);

    vec3_t src = {0.0f, 0.0f, 0.0f};

    vec3_t near = {1.0f, 0.0f, 0.0f};
    float atten_near = npc_audio_graph_query(&graph, src, near);

    vec3_t far = {10.0f, 0.0f, 0.0f};
    float atten_far = npc_audio_graph_query(&graph, src, far);

    ASSERT_TRUE(atten_far > atten_near);

    npc_audio_graph_destroy(&graph);
    PASS();
}

static void test_query_20log10_distance(void) {
    npc_audio_graph_t graph;
    npc_audio_graph_init(&graph);

    vec3_t src = {0.0f, 0.0f, 0.0f};
    vec3_t dst = {2.0f, 0.0f, 0.0f};

    float atten = npc_audio_graph_query(&graph, src, dst);
    float expected = 20.0f * log10f(2.0f);
    ASSERT_FLOAT_NEAR(expected, atten, 0.001f);

    npc_audio_graph_destroy(&graph);
    PASS();
}

static void test_query_stone_medium_adds_absorption(void) {
    npc_audio_graph_t graph;
    npc_audio_graph_init(&graph);
    graph.medium = (npc_acoustic_material_t)NPC_ACOUSTIC_MATERIAL_STONE;

    vec3_t src = {0.0f, 0.0f, 0.0f};
    vec3_t dst = {2.0f, 0.0f, 0.0f};

    float atten = npc_audio_graph_query(&graph, src, dst);
    float expected = 20.0f * log10f(2.0f) + 0.01f;
    ASSERT_FLOAT_NEAR(expected, atten, 0.001f);

    npc_audio_graph_destroy(&graph);
    PASS();
}

static void test_query_inaudible_threshold(void) {
    npc_audio_graph_t graph;
    npc_audio_graph_init(&graph);

    vec3_t src = {0.0f, 0.0f, 0.0f};
    /* Distance such that 20*log10(dist) > NPC_AUDIO_INAUDIBLE_THRESHOLD */
    vec3_t far = {2000.0f, 0.0f, 0.0f};

    float atten = npc_audio_graph_query(&graph, src, far);
    ASSERT_FLOAT_NEAR((float)NPC_AUDIO_INAUDIBLE_THRESHOLD, atten, 0.001f);

    npc_audio_graph_destroy(&graph);
    PASS();
}

static void test_query_null_graph_uses_air(void) {
    vec3_t src = {0.0f, 0.0f, 0.0f};
    vec3_t dst = {5.0f, 0.0f, 0.0f};

    float atten = npc_audio_graph_query(NULL, src, dst);
    float expected = 20.0f * log10f(5.0f);
    ASSERT_FLOAT_NEAR(expected, atten, 0.001f);
    PASS();
}

static void test_query_near_zero_distance_no_negative(void) {
    npc_audio_graph_t graph;
    npc_audio_graph_init(&graph);

    vec3_t src = {0.0f, 0.0f, 0.0f};

    vec3_t dst_a = {0.1f, 0.0f, 0.0f};
    float atten_a = npc_audio_graph_query(&graph, src, dst_a);
    ASSERT_TRUE(atten_a >= 0.0f);

    vec3_t dst_b = {0.5f, 0.0f, 0.0f};
    float atten_b = npc_audio_graph_query(&graph, src, dst_b);
    ASSERT_TRUE(atten_b >= 0.0f);

    npc_audio_graph_destroy(&graph);
    PASS();
}

static void test_query_near_threshold_not_clamped(void) {
    npc_audio_graph_t graph;
    npc_audio_graph_init(&graph);

    /* Distance that puts us just below the threshold */
    /* 20*log10(d) < 60  => d < 10^3 = 1000  */
    vec3_t src = {0.0f, 0.0f, 0.0f};
    vec3_t dst = {500.0f, 0.0f, 0.0f};

    float atten = npc_audio_graph_query(&graph, src, dst);
    float expected = 20.0f * log10f(500.0f);
    ASSERT_TRUE(atten < (float)NPC_AUDIO_INAUDIBLE_THRESHOLD);
    ASSERT_FLOAT_NEAR(expected, atten, 0.01f);

    npc_audio_graph_destroy(&graph);
    PASS();
}

/* ── Lifecycle tests ────────────────────────────────────────────── */

static void test_graph_init_destroy(void) {
    npc_audio_graph_t graph;
    npc_audio_graph_init(&graph);

    ASSERT_TRUE(graph.node_count == 0);
    ASSERT_TRUE(graph.node_cap == 0);
    ASSERT_TRUE(graph.nodes == NULL);
    ASSERT_FLOAT_NEAR(0.00f, graph.medium.absorption_low,  0.001f);
    ASSERT_FLOAT_NEAR(0.00f, graph.medium.absorption_mid,  0.001f);
    ASSERT_FLOAT_NEAR(1.00f, graph.medium.transmission,    0.001f);

    npc_audio_graph_destroy(&graph);
    PASS();
}

static void test_graph_destroy_null_safe(void) {
    npc_audio_graph_destroy(NULL);
    PASS();
}

static void test_graph_init_null_safe(void) {
    npc_audio_graph_init(NULL);
    PASS();
}

/* ── Main ───────────────────────────────────────────────────────── */

int main(void) {
    printf("npc_audio_propagation_tests\n");
    RUN(test_material_wood_preset);
    RUN(test_material_stone_preset);
    RUN(test_material_metal_preset);
    RUN(test_material_glass_preset);
    RUN(test_material_snow_preset);
    RUN(test_material_ice_preset);
    RUN(test_material_flesh_preset);
    RUN(test_material_air_preset);
    RUN(test_query_same_point_zero_attenuation);
    RUN(test_query_distance_attenuation_increases);
    RUN(test_query_20log10_distance);
    RUN(test_query_stone_medium_adds_absorption);
    RUN(test_query_inaudible_threshold);
    RUN(test_query_null_graph_uses_air);
    RUN(test_query_near_threshold_not_clamped);
    RUN(test_query_near_zero_distance_no_negative);
    RUN(test_graph_init_destroy);
    RUN(test_graph_destroy_null_safe);
    RUN(test_graph_init_null_safe);
    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
