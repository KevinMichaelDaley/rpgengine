/**
 * @file render_config_tests.c
 * @brief Unit tests for the headless render-config + world-descriptor loaders
 *        (rpg-da8c). Written before the implementation (TDD phase 1).
 *
 * render_config: defaults match the hall_lit_dynamic reference, a JSON overlay
 * changes only the keys present (missing keys keep defaults), vec fields, and
 * failure modes. world_desc: a world of IRREGULAR variable-size zones (each with
 * its own scene + optional per-zone render config), point->zone lookup, and the
 * per-zone effective-config resolution (zone -> world default -> engine default).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "ferrum/memory/arena.h"
#include "ferrum/scene/render_config.h"
#include "ferrum/scene/world_desc.h"

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

static uint8_t g_arena_buf[2 * 1024 * 1024];
static arena_t g_arena;
static void arena_reset_buf(void) { arena_init(&g_arena, g_arena_buf, sizeof g_arena_buf); }

/* ------------------------------------------------------------------ */
/* render_config                                                       */
/* ------------------------------------------------------------------ */

static int test_defaults_match_reference(void)
{
    render_config_t rc;
    render_config_defaults(&rc);
    /* Values mirrored from hall_lit_dynamic / client_scene_load. */
    ASSERT_FLT_EQ(rc.sh_scale, 0.7f);
    ASSERT_FLT_EQ(rc.sh_normal_bias, 0.9f);
    ASSERT_INT_EQ(rc.dir_cascades, 2);
    ASSERT_INT_EQ(rc.dir_static_res, 1024);
    ASSERT_FLT_EQ(rc.dir_lambda, 0.6f);
    ASSERT_FLT_EQ(rc.dir_softness, 0.7f);
    ASSERT_INT_EQ(rc.shadow_res, 256);
    ASSERT_FLT_EQ(rc.shadow_bias, 0.08f);
    ASSERT_FLT_EQ(rc.sun_energy_scale, 0.45f);
    ASSERT_INT_EQ(rc.gi_enabled, 1);
    ASSERT_FLT_EQ(rc.gi_soft_k, 8.0f);
    ASSERT_FLT_EQ(rc.static_baked_w, 0.35f);
    ASSERT_FLT_EQ(rc.static_dyn_w, 3.0f);
    ASSERT_FLT_EQ(rc.spec_gain, 1.0f);
    ASSERT_INT_EQ(rc.cluster_tiles_x, 16);
    ASSERT_INT_EQ(rc.cluster_slices, 24);
    return 0;
}

/* rpg-96ia: the three low-end-hostile defaults are corrected. gi_bounce=0.9
 * settles at ~10x energy (1/(1-gain)); every shipped config already uses 0.45.
 * msaa/aniso were engine-wide 4/16 -- expensive on iGPUs; discrete levels pin
 * their own 4/16 in render.json. */
static int test_corrected_hostile_defaults(void)
{
    render_config_t rc;
    render_config_defaults(&rc);
    ASSERT_INT_EQ(rc.msaa, 2);
    ASSERT_FLT_EQ(rc.aniso, 4.0f);
    ASSERT_FLT_EQ(rc.gi_bounce, 0.45f);
    return 0;
}

/* rpg-vwyk / rpg-iplq: new perf/quality knobs default to values that PRESERVE
 * today's behavior (a configless level renders as before), so degradation is
 * strictly opt-in via a preset or per-level override. */
static int test_new_knob_defaults_preserve_behavior(void)
{
    render_config_t rc;
    render_config_defaults(&rc);
    /* Quality / scale. */
    ASSERT_FLT_EQ(rc.render_scale, 1.0f);   /* native */
    ASSERT_INT_EQ(rc.pbr_quality, 2);       /* full ubershader */
    ASSERT_INT_EQ(rc.texture_quality, 0);   /* no mips dropped */
    ASSERT_INT_EQ(rc.depth_prepass, -1);    /* auto (on when count>1, as today) */
    /* Shadows (today: R32F, all slots every frame, 8-tap, unlimited). */
    ASSERT_INT_EQ(rc.shadow_fp16, 0);
    ASSERT_INT_EQ(rc.shadow_update_interval, 0);
    ASSERT_FLT_EQ(rc.shadow_distance, 0.0f);
    ASSERT_INT_EQ(rc.shadow_static_cache, 0);
    ASSERT_INT_EQ(rc.dir_pcf_taps, 8);
    ASSERT_INT_EQ(rc.shadow_pcf_taps, 8);
    ASSERT_INT_EQ(rc.dir_dynamic_interval, 1);
    /* Lightmap (today: 9 bands, RGB32F, 8 resident). */
    ASSERT_INT_EQ(rc.lightmap_bands, 9);
    ASSERT_INT_EQ(rc.lm_fp16, 0);
    ASSERT_INT_EQ(rc.lm_resident_layers, 8);
    /* GI march / SDF (today: voxelize every frame, full march, uncapped). */
    ASSERT_INT_EQ(rc.gi_dyn_voxel, 2);
    ASSERT_FLT_EQ(rc.gi_march_quality, 1.0f);
    ASSERT_INT_EQ(rc.gi_frag_quality, 1);
    ASSERT_INT_EQ(rc.gi_prepass_scale, 8);
    ASSERT_INT_EQ(rc.gi_probe_cap, 0);
    ASSERT_FLT_EQ(rc.gi_adaptive_ms, 0.0f);
    ASSERT_INT_EQ(rc.sdf_fp16, 0);
    ASSERT_INT_EQ(rc.sdf_resident_slots, 8);
    ASSERT_INT_EQ(rc.sdf_uploads_per_frame, 0);
    /* Streaming (today: uncapped uploads, 2 GiB budgets). */
    ASSERT_INT_EQ(rc.stream_upload_mb_per_frame, 0);
    ASSERT_INT_EQ(rc.stream_ram_budget_mb, 2048);
    ASSERT_INT_EQ(rc.stream_vram_budget_mb, 2048);
    /* Misc. */
    ASSERT_FLT_EQ(rc.draw_distance, 0.0f);  /* unlimited */
    return 0;
}

/* rpg-vwyk / rpg-iplq: every new key is honored by the overlay parser; keys
 * absent from the document keep their (behavior-preserving) default. */
static int test_new_keys_overlay(void)
{
    const char *j =
        "{ \"render_scale\": 0.75, \"pbr_quality\": 0, \"depth_prepass\": 0,"
        "  \"shadow_fp16\": 1, \"shadow_update_interval\": 4, \"shadow_distance\": 20.0,"
        "  \"dir_pcf_taps\": 4, \"shadow_pcf_taps\": 1, \"lightmap_bands\": 4,"
        "  \"lm_fp16\": 1, \"lm_resident_layers\": 4, \"gi_dyn_voxel\": 0,"
        "  \"gi_march_quality\": 0.5, \"gi_frag_quality\": 0, \"sdf_fp16\": 1,"
        "  \"gi_dyn_gain\": 5.0,"
        "  \"stream_upload_mb_per_frame\": 4, \"draw_distance\": 500.0,"
        "  \"texture_quality\": 2 }";
    render_config_t rc;
    arena_reset_buf();
    ASSERT_TRUE(render_config_parse(j, strlen(j), &g_arena, &rc));
    ASSERT_FLT_EQ(rc.render_scale, 0.75f);
    ASSERT_INT_EQ(rc.pbr_quality, 0);
    ASSERT_INT_EQ(rc.depth_prepass, 0);
    ASSERT_INT_EQ(rc.shadow_fp16, 1);
    ASSERT_INT_EQ(rc.shadow_update_interval, 4);
    ASSERT_FLT_EQ(rc.shadow_distance, 20.0f);
    ASSERT_INT_EQ(rc.dir_pcf_taps, 4);
    ASSERT_INT_EQ(rc.shadow_pcf_taps, 1);
    ASSERT_INT_EQ(rc.lightmap_bands, 4);
    ASSERT_INT_EQ(rc.lm_fp16, 1);
    ASSERT_INT_EQ(rc.lm_resident_layers, 4);
    ASSERT_INT_EQ(rc.gi_dyn_voxel, 0);
    ASSERT_FLT_EQ(rc.gi_dyn_gain, 5.0f);
    ASSERT_FLT_EQ(rc.gi_march_quality, 0.5f);
    ASSERT_INT_EQ(rc.gi_frag_quality, 0);
    ASSERT_INT_EQ(rc.sdf_fp16, 1);
    ASSERT_INT_EQ(rc.stream_upload_mb_per_frame, 4);
    ASSERT_FLT_EQ(rc.draw_distance, 500.0f);
    ASSERT_INT_EQ(rc.texture_quality, 2);
    /* Untouched new keys keep defaults. */
    ASSERT_INT_EQ(rc.gi_prepass_scale, 8);
    ASSERT_INT_EQ(rc.stream_vram_budget_mb, 2048);
    /* Untouched legacy keys keep defaults. */
    ASSERT_INT_EQ(rc.shadow_res, 256);
    return 0;
}

/* rpg-uhhr: the shipped preset/default JSON files parse as valid overlays and
 * apply their documented tier values. Run from the repo root (as `make test`
 * does), so the paths resolve; a missing file or a JSON typo fails the load and
 * this test. Guards the presets against silent breakage. */
static int test_preset_files_load(void)
{
    render_config_t rc;

    arena_reset_buf();
    ASSERT_TRUE(render_config_load("assets/config/render_config.default.json",
                                   &g_arena, &rc));
    ASSERT_INT_EQ(rc.msaa, 2);            /* mirrors the corrected default */
    ASSERT_FLT_EQ(rc.gi_bounce, 0.45f);

    arena_reset_buf();
    ASSERT_TRUE(render_config_load("assets/config/render_config.low.json",
                                   &g_arena, &rc));
    ASSERT_INT_EQ(rc.msaa, 0);
    ASSERT_FLT_EQ(rc.render_scale, 0.75f);
    ASSERT_INT_EQ(rc.shadow_fp16, 1);
    ASSERT_INT_EQ(rc.gi_dyn_voxel, 0);
    ASSERT_INT_EQ(rc.pbr_quality, 0);

    arena_reset_buf();
    ASSERT_TRUE(render_config_load("assets/config/render_config.med.json",
                                   &g_arena, &rc));
    ASSERT_INT_EQ(rc.msaa, 2);
    ASSERT_INT_EQ(rc.gi_update_interval, 8);
    ASSERT_INT_EQ(rc.gi_n_probe_groups, 2);

    arena_reset_buf();
    ASSERT_TRUE(render_config_load("assets/config/render_config.high.json",
                                   &g_arena, &rc));
    ASSERT_INT_EQ(rc.msaa, 4);
    ASSERT_FLT_EQ(rc.aniso, 16.0f);
    ASSERT_INT_EQ(rc.dir_cascades, 3);
    ASSERT_INT_EQ(rc.lm_fp16, 0);
    return 0;
}

static int test_json_overlay_partial(void)
{
    /* Only a few keys present: they override; everything else keeps the default. */
    const char *j =
        "{ \"sh_scale\": 0.35, \"gi_soft_k\": 12.0, \"dir_cascades\": 3,"
        "  \"sky_ao_mult\": 0.9 }";
    render_config_t rc;
    arena_reset_buf();
    ASSERT_TRUE(render_config_parse(j, strlen(j), &g_arena, &rc));
    ASSERT_FLT_EQ(rc.sh_scale, 0.35f);        /* overridden */
    ASSERT_FLT_EQ(rc.gi_soft_k, 12.0f);       /* overridden */
    ASSERT_INT_EQ(rc.dir_cascades, 3);        /* overridden */
    ASSERT_FLT_EQ(rc.sky_ao_mult, 0.9f);      /* overridden */
    ASSERT_FLT_EQ(rc.sh_normal_bias, 0.9f);   /* default survives */
    ASSERT_INT_EQ(rc.shadow_res, 256);        /* default survives */
    ASSERT_FLT_EQ(rc.spec_gain, 1.0f);        /* default survives */
    ASSERT_INT_EQ(rc.dir_translucency, 1);    /* default survives */
    return 0;
}

static int test_translucency_overrides(void)
{
    /* Both transparent-shadow knobs can be forced off from the scene JSON. */
    const char *j = "{ \"dir_translucency\": 0, \"dir_caustics\": 0 }";
    render_config_t rc;
    arena_reset_buf();
    ASSERT_TRUE(render_config_parse(j, strlen(j), &g_arena, &rc));
    ASSERT_INT_EQ(rc.dir_translucency, 0);
    ASSERT_INT_EQ(rc.dir_caustics, 0);
    return 0;
}

static int test_json_vec_fields(void)
{
    const char *j =
        "{ \"ambient\": [0.1, 0.2, 0.3], \"sky_ao_color\": [0.05, 0.06, 0.07] }";
    render_config_t rc;
    arena_reset_buf();
    ASSERT_TRUE(render_config_parse(j, strlen(j), &g_arena, &rc));
    ASSERT_FLT_EQ(rc.ambient[0], 0.1f);
    ASSERT_FLT_EQ(rc.ambient[1], 0.2f);
    ASSERT_FLT_EQ(rc.ambient[2], 0.3f);
    ASSERT_FLT_EQ(rc.sky_ao_color[0], 0.05f);
    ASSERT_FLT_EQ(rc.sky_ao_color[2], 0.07f);
    return 0;
}

static int test_json_empty_is_defaults(void)
{
    render_config_t rc, def;
    render_config_defaults(&def);
    arena_reset_buf();
    ASSERT_TRUE(render_config_parse("{}", 2, &g_arena, &rc));
    ASSERT_TRUE(memcmp(&rc, &def, sizeof rc) == 0);
    return 0;
}

static int test_json_failure_modes(void)
{
    render_config_t rc;
    arena_reset_buf();
    ASSERT_FALSE(render_config_parse("not json", 8, &g_arena, &rc));
    ASSERT_FALSE(render_config_parse("[1,2,3]", 7, &g_arena, &rc)); /* non-object root */
    ASSERT_FALSE(render_config_parse(NULL, 0, &g_arena, &rc));
    return 0;
}

/* ------------------------------------------------------------------ */
/* world_desc                                                          */
/* ------------------------------------------------------------------ */

/* Two IRREGULAR zones of different sizes; the second spans a much larger box.
 * Zone 1 carries its own render config; zone 0 falls back to the world default. */
static const char *WORLD_JSON =
    "{ \"name\": \"demo_world\","
    "  \"render_config\": \"cfg/world_default.json\","
    "  \"zones\": ["
    "    { \"name\": \"keep\",  \"min\": [0,0,0],     \"max\": [10,8,10],"
    "      \"scene\": \"keep/keep.scene\" },"
    "    { \"name\": \"bailey\",\"min\": [20,0,-30],  \"max\": [120,40,60],"
    "      \"scene\": \"bailey/bailey.scene\","
    "      \"render_config\": \"bailey/bright.json\" }"
    "  ] }";

static int test_world_parse(void)
{
    world_desc_t w;
    arena_reset_buf();
    ASSERT_TRUE(world_desc_parse(WORLD_JSON, strlen(WORLD_JSON), &g_arena, &w));
    ASSERT_STR_EQ(w.name, "demo_world");
    ASSERT_INT_EQ(w.zone_count, 2);
    ASSERT_STR_EQ(w.default_render_config, "cfg/world_default.json");
    /* Zone 0: small keep, own config empty -> falls back to world default. */
    ASSERT_STR_EQ(w.zones[0].name, "keep");
    ASSERT_FLT_EQ(w.zones[0].box_min[0], 0.0f);
    ASSERT_FLT_EQ(w.zones[0].box_max[1], 8.0f);
    ASSERT_STR_EQ(w.zones[0].scene, "keep/keep.scene");
    ASSERT_STR_EQ(w.zones[0].render_config, "");
    /* Zone 1: large bailey, irregular (different size), own config. */
    ASSERT_FLT_EQ(w.zones[1].box_min[2], -30.0f);
    ASSERT_FLT_EQ(w.zones[1].box_max[0], 120.0f);
    ASSERT_STR_EQ(w.zones[1].render_config, "bailey/bright.json");
    return 0;
}

static int test_world_zone_at(void)
{
    world_desc_t w;
    arena_reset_buf();
    ASSERT_TRUE(world_desc_parse(WORLD_JSON, strlen(WORLD_JSON), &g_arena, &w));
    float in_keep[3]   = { 5, 4, 5 };
    float in_bailey[3] = { 100, 10, 40 };
    float nowhere[3]   = { -100, 0, 0 };
    ASSERT_INT_EQ(world_desc_zone_at(&w, in_keep), 0);
    ASSERT_INT_EQ(world_desc_zone_at(&w, in_bailey), 1);
    ASSERT_INT_EQ(world_desc_zone_at(&w, nowhere), -1);
    return 0;
}

static int test_world_effective_config(void)
{
    world_desc_t w;
    arena_reset_buf();
    ASSERT_TRUE(world_desc_parse(WORLD_JSON, strlen(WORLD_JSON), &g_arena, &w));
    /* zone 0 has no own config -> world default; zone 1 has its own. */
    ASSERT_STR_EQ(world_desc_zone_config(&w, 0), "cfg/world_default.json");
    ASSERT_STR_EQ(world_desc_zone_config(&w, 1), "bailey/bright.json");
    ASSERT_TRUE(world_desc_zone_config(&w, 99) == NULL);  /* out of range */
    return 0;
}

static int test_world_failure_modes(void)
{
    world_desc_t w;
    arena_reset_buf();
    ASSERT_FALSE(world_desc_parse("garbage", 7, &g_arena, &w));
    ASSERT_FALSE(world_desc_parse("{}", 2, &g_arena, &w));  /* no zones -> invalid */
    return 0;
}

/* ------------------------------------------------------------------ */

int main(void)
{
    struct { const char *name; int (*fn)(void); } tests[] = {
        { "defaults_match_reference", test_defaults_match_reference },
        { "corrected_hostile_defaults", test_corrected_hostile_defaults },
        { "new_knob_defaults_preserve_behavior", test_new_knob_defaults_preserve_behavior },
        { "new_keys_overlay",         test_new_keys_overlay },
        { "preset_files_load",        test_preset_files_load },
        { "json_overlay_partial",     test_json_overlay_partial },
        { "translucency_overrides",   test_translucency_overrides },
        { "json_vec_fields",          test_json_vec_fields },
        { "json_empty_is_defaults",   test_json_empty_is_defaults },
        { "json_failure_modes",       test_json_failure_modes },
        { "world_parse",              test_world_parse },
        { "world_zone_at",            test_world_zone_at },
        { "world_effective_config",   test_world_effective_config },
        { "world_failure_modes",      test_world_failure_modes },
    };
    int n = (int)(sizeof tests / sizeof tests[0]), pass = 0;
    for (int i = 0; i < n; ++i) {
        int rc = tests[i].fn();
        printf("[%s] %s\n", rc == 0 ? "ok  " : "FAIL", tests[i].name);
        pass += (rc == 0);
    }
    printf("\nrender_config_tests: %d/%d passed\n", pass, n);
    return pass == n ? 0 : 1;
}
