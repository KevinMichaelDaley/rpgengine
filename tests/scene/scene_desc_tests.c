/**
 * @file scene_desc_tests.c
 * @brief Unit + regression tests for the headless scene/level descriptor loader
 *        (rpg-51nf). Written before the implementation exists (TDD phase 1).
 *
 * Coverage: happy-path parse of every asset class, bake-order preservation,
 * material name->index mapping, optional-section defaults, edge cases (empty
 * object list, multi-material objects), failure modes (malformed JSON, non-object
 * root, arena exhaustion, missing required section), and a round-trip of the
 * baked great_hall descriptor.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "ferrum/memory/arena.h"
#include "ferrum/scene/scene_desc.h"

/* ----------------------------------------------------------------------- */
/* Harness                                                                  */
/* ----------------------------------------------------------------------- */

#define ASSERT_TRUE(expr)                                                     \
    do { if (!(expr)) { fprintf(stderr, "  ASSERT_TRUE failed: %s (%s:%d)\n", \
        #expr, __FILE__, __LINE__); return 1; } } while (0)
#define ASSERT_FALSE(expr) ASSERT_TRUE(!(expr))
#define ASSERT_STR_EQ(a, b)                                                   \
    do { if (strcmp((a), (b)) != 0) { fprintf(stderr,                        \
        "  ASSERT_STR_EQ failed: \"%s\" != \"%s\" (%s:%d)\n", (a), (b),       \
        __FILE__, __LINE__); return 1; } } while (0)
#define ASSERT_INT_EQ(a, b)                                                   \
    do { long _a=(long)(a), _b=(long)(b); if (_a!=_b) { fprintf(stderr,      \
        "  ASSERT_INT_EQ failed: %ld != %ld (%s:%d)\n", _a, _b,               \
        __FILE__, __LINE__); return 1; } } while (0)
#define ASSERT_FLT_EQ(a, b)                                                   \
    do { double _a=(double)(a), _b=(double)(b); if (fabs(_a-_b) > 1e-4) {     \
        fprintf(stderr, "  ASSERT_FLT_EQ failed: %g != %g (%s:%d)\n", _a, _b, \
        __FILE__, __LINE__); return 1; } } while (0)

static uint8_t g_arena_buf[4 * 1024 * 1024];

/* A compact but complete synthetic descriptor exercising every field. Object
 * order (b_second before a_first) is intentional: the loader MUST preserve JSON
 * array order because that order IS the lightmap bake order. */
static const char *const DESC_FULL =
"{"
"  \"name\": \"unit_hall\","
"  \"materials\": [\"stone\", \"timber\", \"metal\"],"
"  \"objects\": ["
"    {\"name\":\"b_second\",\"mesh\":\"meshes/b.fvma\","
"     \"position\":[1.0,2.0,3.0],\"rotation\":[0.0,0.0,0.0,1.0],\"scale\":[1,1,1],"
"     \"materials\":[\"timber\"],\"lightmap_res\":72,\"sh_layer\":0},"
"    {\"name\":\"a_first\",\"mesh\":\"meshes/a.dmesh\","
"     \"position\":[0,0,0],\"rotation\":[0,0,0,1],\"scale\":[2,2,2],"
"     \"materials\":[\"stone\",\"metal\"],\"lightmap_res\":128,\"sh_layer\":3}"
"  ],"
"  \"lightmap\": {\"prefix\":\"unit.flm\",\"perchunk\":true,\"manifest\":\"unit.flm_manifest.bin\"},"
"  \"sdf\": {\"prefix\":\"unit.flm\"},"
"  \"probes\": {\"spacing\":1.1,\"vspacing\":0.8,\"manual\":\"unit.probes\","
"    \"importance\":[{\"min\":[-1,-2,-3],\"max\":[4,5,6],\"density\":2.5,\"priority\":1.5}]}"
"}";

static int test_happy_name_and_materials(void)
{
    arena_t a; arena_init(&a, g_arena_buf, sizeof g_arena_buf);
    scene_desc_t d;
    ASSERT_TRUE(scene_desc_parse(DESC_FULL, strlen(DESC_FULL), &a, &d));
    ASSERT_STR_EQ(d.name, "unit_hall");
    ASSERT_INT_EQ(d.material_count, 3);
    ASSERT_STR_EQ(d.materials[0], "stone");
    ASSERT_STR_EQ(d.materials[1], "timber");
    ASSERT_STR_EQ(d.materials[2], "metal");
    return 0;
}

static int test_object_bake_order_preserved(void)
{
    arena_t a; arena_init(&a, g_arena_buf, sizeof g_arena_buf);
    scene_desc_t d;
    ASSERT_TRUE(scene_desc_parse(DESC_FULL, strlen(DESC_FULL), &a, &d));
    ASSERT_INT_EQ(d.object_count, 2);
    /* JSON array order preserved verbatim (== bake order), NOT sorted. */
    ASSERT_STR_EQ(d.objects[0].name, "b_second");
    ASSERT_STR_EQ(d.objects[1].name, "a_first");
    ASSERT_STR_EQ(d.objects[0].mesh, "meshes/b.fvma");
    ASSERT_STR_EQ(d.objects[1].mesh, "meshes/a.dmesh");
    return 0;
}

static int test_object_transform_and_lightmap(void)
{
    arena_t a; arena_init(&a, g_arena_buf, sizeof g_arena_buf);
    scene_desc_t d;
    ASSERT_TRUE(scene_desc_parse(DESC_FULL, strlen(DESC_FULL), &a, &d));
    const scene_desc_object_t *o0 = &d.objects[0];
    ASSERT_FLT_EQ(o0->position[0], 1.0f); ASSERT_FLT_EQ(o0->position[1], 2.0f);
    ASSERT_FLT_EQ(o0->position[2], 3.0f);
    ASSERT_FLT_EQ(o0->rotation[3], 1.0f);
    ASSERT_FLT_EQ(o0->scale[0], 1.0f);
    ASSERT_INT_EQ(o0->lightmap_res, 72);
    ASSERT_INT_EQ(o0->sh_layer, 0);
    const scene_desc_object_t *o1 = &d.objects[1];
    ASSERT_FLT_EQ(o1->scale[0], 2.0f);
    ASSERT_INT_EQ(o1->lightmap_res, 128);
    ASSERT_INT_EQ(o1->sh_layer, 3);
    return 0;
}

static int test_material_name_to_index(void)
{
    arena_t a; arena_init(&a, g_arena_buf, sizeof g_arena_buf);
    scene_desc_t d;
    ASSERT_TRUE(scene_desc_parse(DESC_FULL, strlen(DESC_FULL), &a, &d));
    /* b_second -> "timber" (index 1). */
    ASSERT_INT_EQ(d.objects[0].material_count, 1);
    ASSERT_INT_EQ(d.objects[0].material_idx[0], 1);
    /* a_first -> "stone"(0), "metal"(2). */
    ASSERT_INT_EQ(d.objects[1].material_count, 2);
    ASSERT_INT_EQ(d.objects[1].material_idx[0], 0);
    ASSERT_INT_EQ(d.objects[1].material_idx[1], 2);
    return 0;
}

static int test_lightdata_section(void)
{
    arena_t a; arena_init(&a, g_arena_buf, sizeof g_arena_buf);
    scene_desc_t d;
    ASSERT_TRUE(scene_desc_parse(DESC_FULL, strlen(DESC_FULL), &a, &d));
    ASSERT_STR_EQ(d.lightdata.lightmap_prefix, "unit.flm");
    ASSERT_TRUE(d.lightdata.lightmap_perchunk);
    ASSERT_STR_EQ(d.lightdata.lightmap_manifest, "unit.flm_manifest.bin");
    ASSERT_STR_EQ(d.lightdata.sdf_prefix, "unit.flm");
    return 0;
}

static int test_probe_spec_and_importance(void)
{
    arena_t a; arena_init(&a, g_arena_buf, sizeof g_arena_buf);
    scene_desc_t d;
    ASSERT_TRUE(scene_desc_parse(DESC_FULL, strlen(DESC_FULL), &a, &d));
    ASSERT_FLT_EQ(d.probes.spacing, 1.1f);
    ASSERT_FLT_EQ(d.probes.vspacing, 0.8f);
    ASSERT_TRUE(d.probes.has_manual);
    ASSERT_STR_EQ(d.probes.manual_path, "unit.probes");
    ASSERT_INT_EQ(d.probes.box_count, 1);
    const scene_desc_importance_box_t *b = &d.probes.boxes[0];
    ASSERT_FLT_EQ(b->min[0], -1.0f); ASSERT_FLT_EQ(b->max[2], 6.0f);
    ASSERT_FLT_EQ(b->density_mult, 2.5f);
    ASSERT_FLT_EQ(b->priority_bias, 1.5f);
    return 0;
}

static int test_optional_sections_default(void)
{
    /* No lightmap/sdf/probes sections: loader must succeed with safe defaults. */
    static const char *const MIN =
        "{\"name\":\"bare\",\"materials\":[\"m\"],"
        "\"objects\":[{\"name\":\"o\",\"mesh\":\"o.obj\"}]}";
    arena_t a; arena_init(&a, g_arena_buf, sizeof g_arena_buf);
    scene_desc_t d;
    ASSERT_TRUE(scene_desc_parse(MIN, strlen(MIN), &a, &d));
    ASSERT_INT_EQ(d.object_count, 1);
    /* Object with no transform -> identity; no sh_layer -> 0; no materials -> 0. */
    ASSERT_FLT_EQ(d.objects[0].scale[0], 1.0f);
    ASSERT_FLT_EQ(d.objects[0].rotation[3], 1.0f);
    ASSERT_INT_EQ(d.objects[0].sh_layer, 0);
    ASSERT_INT_EQ(d.objects[0].material_count, 0);
    /* Missing optional sections -> empty / engine-default sentinels. */
    ASSERT_INT_EQ(d.lightdata.lightmap_prefix[0], 0);
    ASSERT_INT_EQ(d.lightdata.sdf_prefix[0], 0);
    ASSERT_TRUE(d.probes.spacing <= 0.0f);   /* <=0 => use engine default */
    ASSERT_INT_EQ(d.probes.box_count, 0);
    ASSERT_FALSE(d.probes.has_manual);
    return 0;
}

static int test_empty_objects_ok(void)
{
    static const char *const E = "{\"name\":\"e\",\"materials\":[],\"objects\":[]}";
    arena_t a; arena_init(&a, g_arena_buf, sizeof g_arena_buf);
    scene_desc_t d;
    ASSERT_TRUE(scene_desc_parse(E, strlen(E), &a, &d));
    ASSERT_INT_EQ(d.object_count, 0);
    ASSERT_INT_EQ(d.material_count, 0);
    return 0;
}

static int test_malformed_json_fails(void)
{
    static const char *const BAD = "{\"name\":\"x\", \"objects\": [ {oops }";
    arena_t a; arena_init(&a, g_arena_buf, sizeof g_arena_buf);
    scene_desc_t d;
    ASSERT_FALSE(scene_desc_parse(BAD, strlen(BAD), &a, &d));
    return 0;
}

static int test_non_object_root_fails(void)
{
    static const char *const ARR = "[1,2,3]";
    arena_t a; arena_init(&a, g_arena_buf, sizeof g_arena_buf);
    scene_desc_t d;
    ASSERT_FALSE(scene_desc_parse(ARR, strlen(ARR), &a, &d));
    return 0;
}

static int test_missing_objects_fails(void)
{
    static const char *const NOOBJ = "{\"name\":\"x\",\"materials\":[\"m\"]}";
    arena_t a; arena_init(&a, g_arena_buf, sizeof g_arena_buf);
    scene_desc_t d;
    ASSERT_FALSE(scene_desc_parse(NOOBJ, strlen(NOOBJ), &a, &d));
    return 0;
}

static int test_arena_exhaustion_fails(void)
{
    /* Far too small an arena: must fail cleanly (no crash), not partially fill. */
    static uint8_t tiny[64];
    arena_t a; arena_init(&a, tiny, sizeof tiny);
    scene_desc_t d;
    ASSERT_FALSE(scene_desc_parse(DESC_FULL, strlen(DESC_FULL), &a, &d));
    return 0;
}

/* Round-trip the real baked great_hall descriptor (datasets/great_hall_export).
 * Path is relative to the repo root (where the test binary is run). */
static int test_load_great_hall(void)
{
    const char *path = "datasets/great_hall_export/great_hall.scene";
    arena_t a; arena_init(&a, g_arena_buf, sizeof g_arena_buf);
    scene_desc_t d;
    if (!scene_desc_load(path, &a, &d)) {
        fprintf(stderr, "  (skip) great_hall descriptor not found at %s\n", path);
        return 0;   /* data-dependent: don't fail CI if the asset is absent */
    }
    ASSERT_INT_EQ(d.material_count, 5);
    ASSERT_STR_EQ(d.materials[0], "great_hall_floor_stone");
    ASSERT_INT_EQ(d.object_count, 84);
    /* First object == first baked mesh (bake order). */
    ASSERT_STR_EQ(d.objects[0].name, "great_hall_collar_0");
    ASSERT_STR_EQ(d.objects[0].mesh, "meshes/great_hall_collar_0.fvma");
    ASSERT_STR_EQ(d.lightdata.lightmap_prefix, "great_hall.flm");
    ASSERT_STR_EQ(d.lightdata.sdf_prefix, "great_hall.flm");
    ASSERT_FLT_EQ(d.probes.spacing, 1.1f);
    ASSERT_FLT_EQ(d.probes.vspacing, 0.8f);
    return 0;
}

int main(void)
{
    struct { const char *name; int (*fn)(void); } tests[] = {
        {"happy_name_and_materials",    test_happy_name_and_materials},
        {"object_bake_order_preserved", test_object_bake_order_preserved},
        {"object_transform_and_lightmap", test_object_transform_and_lightmap},
        {"material_name_to_index",      test_material_name_to_index},
        {"lightdata_section",           test_lightdata_section},
        {"probe_spec_and_importance",   test_probe_spec_and_importance},
        {"optional_sections_default",   test_optional_sections_default},
        {"empty_objects_ok",            test_empty_objects_ok},
        {"malformed_json_fails",        test_malformed_json_fails},
        {"non_object_root_fails",       test_non_object_root_fails},
        {"missing_objects_fails",       test_missing_objects_fails},
        {"arena_exhaustion_fails",      test_arena_exhaustion_fails},
        {"load_great_hall",             test_load_great_hall},
    };
    int n = (int)(sizeof tests / sizeof tests[0]), fails = 0;
    for (int i = 0; i < n; ++i) {
        int r = tests[i].fn();
        fprintf(stderr, "[%s] %s\n", r ? "FAIL" : "ok  ", tests[i].name);
        fails += (r != 0);
    }
    fprintf(stderr, "\nscene_desc_tests: %d/%d passed\n", n - fails, n);
    return fails ? 1 : 0;
}
