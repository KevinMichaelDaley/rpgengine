/**
 * @file srd_rules_feature_tests.c
 * @brief Tests for feature rules (Rules 31-46): doors, stairs, special rooms.
 *
 * Covers: registration counts, cond predicates, apply effects, roundtrips,
 * inverse rule ID correctness, boss room uniqueness constraint, and
 * treasure room single-neighbour invariant.
 */
#include "ferrum/procgen/srd/srd_rules_feature.h"
#include "ferrum/procgen/srd/srd_descent_rules.h"
#include "ferrum/procgen/srd/srd_sdf_layout.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* ── Test macros ───────────────────────────────────────────────────── */

#define ASSERT_TRUE(cond)                                                    \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "ASSERT_TRUE failed: %s:%d: %s\n",              \
                    __FILE__, __LINE__, #cond);                              \
            return 1;                                                        \
        }                                                                    \
    } while (0)

#define ASSERT_INT_EQ(exp, act)                                              \
    do {                                                                     \
        if ((exp) != (act)) {                                                \
            fprintf(stderr, "ASSERT_INT_EQ failed: %s:%d: expected %d "      \
                    "got %d\n", __FILE__, __LINE__, (int)(exp), (int)(act));  \
            return 1;                                                        \
        }                                                                    \
    } while (0)

#define ASSERT_FLOAT_NEAR(exp, act, tol)                                     \
    do {                                                                     \
        float _e = (exp), _a = (act);                                        \
        if (fabsf(_e - _a) > (tol)) {                                        \
            fprintf(stderr, "ASSERT_FLOAT_NEAR failed: %s:%d: expected "     \
                    "%.6f got %.6f (tol=%.6f)\n",                            \
                    __FILE__, __LINE__, (double)_e, (double)_a,              \
                    (double)(tol));                                           \
            return 1;                                                        \
        }                                                                    \
    } while (0)

/* ── Helper: build standard test layouts ──────────────────────────── */

/**
 * Build a layout with one room at (5, 5) hw=3 hd=2 for testing.
 */
static void build_single_room(srd_sdf_layout_t *layout) {
    srd_sdf_layout_init(layout);
    layout->bounds_w = 20.0f;
    layout->bounds_h = 20.0f;
    srd_sdf_box_t box = {5.0f, 5.0f, 3.0f, 2.0f, SRD_ROOM_GENERIC, 0, {0}};
    srd_sdf_layout_add_box(layout, &box);
}

/**
 * Build a layout with two adjacent rooms.
 */
static void build_two_rooms(srd_sdf_layout_t *layout) {
    srd_sdf_layout_init(layout);
    layout->bounds_w = 20.0f;
    layout->bounds_h = 20.0f;
    srd_sdf_box_t a = {3.0f, 5.0f, 2.0f, 2.0f, SRD_ROOM_GENERIC, 0, {0}};
    srd_sdf_box_t b = {7.0f, 5.0f, 2.0f, 2.0f, SRD_ROOM_GENERIC, 0, {0}};
    srd_sdf_layout_add_box(layout, &a);
    srd_sdf_layout_add_box(layout, &b);
    srd_sdf_layout_set_adj(layout, 0, 1, true);
}

/**
 * Register all feature rules and return base index.
 */
static int register_all_feature_rules(srd_rule_table_t *tbl) {
    srd_rule_table_init(tbl);
    int r1 = srd_rules_feature_register_door(tbl);
    int r2 = srd_rules_feature_register_stair(tbl);
    int r3 = srd_rules_feature_register_special(tbl);
    int r4 = srd_rules_feature_register_boss(tbl);
    if (r1 < 0 || r2 < 0 || r3 < 0 || r4 < 0) return -1;
    return 0;
}

/* ── Registration tests ────────────────────────────────────────────── */

static int test_register_door_count(void) {
    srd_rule_table_t tbl;
    srd_rule_table_init(&tbl);
    int r = srd_rules_feature_register_door(&tbl);
    ASSERT_INT_EQ(4, r);
    ASSERT_INT_EQ(4, tbl.n_rules);
    return 0;
}

static int test_register_stair_count(void) {
    srd_rule_table_t tbl;
    srd_rule_table_init(&tbl);
    int r = srd_rules_feature_register_stair(&tbl);
    ASSERT_INT_EQ(4, r);
    ASSERT_INT_EQ(4, tbl.n_rules);
    return 0;
}

static int test_register_special_count(void) {
    srd_rule_table_t tbl;
    srd_rule_table_init(&tbl);
    int r = srd_rules_feature_register_special(&tbl);
    ASSERT_INT_EQ(4, r);
    ASSERT_INT_EQ(4, tbl.n_rules);
    return 0;
}

static int test_register_boss_count(void) {
    srd_rule_table_t tbl;
    srd_rule_table_init(&tbl);
    int r = srd_rules_feature_register_boss(&tbl);
    ASSERT_INT_EQ(4, r);
    ASSERT_INT_EQ(4, tbl.n_rules);
    return 0;
}

static int test_register_all_16_rules(void) {
    srd_rule_table_t tbl;
    int rc = register_all_feature_rules(&tbl);
    ASSERT_TRUE(rc >= 0);
    ASSERT_INT_EQ(16, tbl.n_rules);
    return 0;
}

static int test_all_rules_have_names(void) {
    srd_rule_table_t tbl;
    register_all_feature_rules(&tbl);
    for (int i = 0; i < tbl.n_rules; i++) {
        ASSERT_TRUE(tbl.rules[i].name != NULL);
        ASSERT_TRUE(tbl.rules[i].name[0] != '\0');
    }
    return 0;
}

static int test_all_rules_have_cond_and_apply(void) {
    srd_rule_table_t tbl;
    register_all_feature_rules(&tbl);
    for (int i = 0; i < tbl.n_rules; i++) {
        ASSERT_TRUE(tbl.rules[i].cond != NULL);
        ASSERT_TRUE(tbl.rules[i].apply != NULL);
    }
    return 0;
}

static int test_inverse_ids_valid(void) {
    srd_rule_table_t tbl;
    register_all_feature_rules(&tbl);
    for (int i = 0; i < tbl.n_rules; i++) {
        int inv = tbl.rules[i].inverse_rule_id;
        if (inv >= 0) {
            ASSERT_TRUE(inv < tbl.n_rules);
            ASSERT_TRUE(tbl.rules[inv].name != NULL);
        }
    }
    return 0;
}

/* ── Door tests (Rules 31-34) ──────────────────────────────────────── */

/** AddDoor sets door_width to SRD_EPSILON on the north wall. */
static int test_add_door_sets_width(void) {
    srd_rule_table_t tbl;
    register_all_feature_rules(&tbl);

    srd_sdf_layout_t layout;
    build_single_room(&layout);

    /* AddDoor is rule index 1 (RemoveDoor=0, AddDoor=1) */
    /* Actually: registration order is RemoveDoor first, then AddDoor,
     * then NarrowDoor, then WidenDoor. Let me find the indices by name. */
    int add_door_idx = -1;
    for (int i = 0; i < tbl.n_rules; i++) {
        if (strcmp(tbl.rules[i].name, "AddDoor") == 0) {
            add_door_idx = i;
            break;
        }
    }
    ASSERT_TRUE(add_door_idx >= 0);

    /* Use default userdata NULL => side 0 (North) */
    srd_selection_t sel = { .indices = {0}, .n = 1 };
    ASSERT_TRUE(tbl.rules[add_door_idx].cond(&layout, &sel, NULL));

    int new_indices[8];
    int rc = tbl.rules[add_door_idx].apply(&layout, &sel, NULL, new_indices, 8);
    ASSERT_TRUE(rc >= 0);

    /* door_width[0] (North) should now be SRD_EPSILON */
    ASSERT_FLOAT_NEAR(SRD_EPSILON, layout.boxes[0].door_width[0], 0.001f);
    return 0;
}

/** RemoveDoor clears door_width back to 0. */
static int test_remove_door_clears_width(void) {
    srd_rule_table_t tbl;
    register_all_feature_rules(&tbl);

    srd_sdf_layout_t layout;
    build_single_room(&layout);

    int add_idx = -1, rem_idx = -1;
    for (int i = 0; i < tbl.n_rules; i++) {
        if (strcmp(tbl.rules[i].name, "AddDoor") == 0) add_idx = i;
        if (strcmp(tbl.rules[i].name, "RemoveDoor") == 0) rem_idx = i;
    }
    ASSERT_TRUE(add_idx >= 0 && rem_idx >= 0);

    /* Add door first */
    srd_selection_t sel = { .indices = {0}, .n = 1 };
    int new_indices[8];
    tbl.rules[add_idx].apply(&layout, &sel, NULL, new_indices, 8);
    ASSERT_FLOAT_NEAR(SRD_EPSILON, layout.boxes[0].door_width[0], 0.001f);

    /* Remove door */
    tbl.rules[rem_idx].apply(&layout, &sel, NULL, new_indices, 8);
    ASSERT_FLOAT_NEAR(0.0f, layout.boxes[0].door_width[0], 0.001f);
    return 0;
}

/** AddDoor then RemoveDoor roundtrip: door_width returns to zero. */
static int test_add_remove_door_roundtrip(void) {
    srd_rule_table_t tbl;
    register_all_feature_rules(&tbl);

    srd_sdf_layout_t layout;
    build_single_room(&layout);

    /* Save original door widths */
    float orig[SRD_MAX_DOORS];
    memcpy(orig, layout.boxes[0].door_width, sizeof(orig));

    int add_idx = -1, rem_idx = -1;
    for (int i = 0; i < tbl.n_rules; i++) {
        if (strcmp(tbl.rules[i].name, "AddDoor") == 0) add_idx = i;
        if (strcmp(tbl.rules[i].name, "RemoveDoor") == 0) rem_idx = i;
    }

    srd_selection_t sel = { .indices = {0}, .n = 1 };
    int new_indices[8];

    /* Add */
    tbl.rules[add_idx].apply(&layout, &sel, NULL, new_indices, 8);
    /* Remove */
    tbl.rules[rem_idx].apply(&layout, &sel, NULL, new_indices, 8);

    /* All door widths should be back to original */
    for (int d = 0; d < SRD_MAX_DOORS; d++) {
        ASSERT_FLOAT_NEAR(orig[d], layout.boxes[0].door_width[d], 0.001f);
    }
    return 0;
}

/** AddDoor cond rejects if door already exists on that side. */
static int test_add_door_cond_rejects_existing(void) {
    srd_rule_table_t tbl;
    register_all_feature_rules(&tbl);

    srd_sdf_layout_t layout;
    build_single_room(&layout);
    /* Pre-set a door on north wall */
    layout.boxes[0].door_width[0] = 1.0f;

    int add_idx = -1;
    for (int i = 0; i < tbl.n_rules; i++) {
        if (strcmp(tbl.rules[i].name, "AddDoor") == 0) { add_idx = i; break; }
    }

    srd_selection_t sel = { .indices = {0}, .n = 1 };
    /* With NULL userdata => side 0 (North), already has a door */
    ASSERT_TRUE(!tbl.rules[add_idx].cond(&layout, &sel, NULL));
    return 0;
}

/** WidenDoor multiplies door_width by 1.5. */
static int test_widen_door_multiplies(void) {
    srd_rule_table_t tbl;
    register_all_feature_rules(&tbl);

    srd_sdf_layout_t layout;
    build_single_room(&layout);
    layout.boxes[0].door_width[0] = 2.0f;  /* existing door */

    int widen_idx = -1;
    for (int i = 0; i < tbl.n_rules; i++) {
        if (strcmp(tbl.rules[i].name, "WidenDoor") == 0) { widen_idx = i; break; }
    }
    ASSERT_TRUE(widen_idx >= 0);

    srd_selection_t sel = { .indices = {0}, .n = 1 };
    int new_indices[8];
    tbl.rules[widen_idx].apply(&layout, &sel, NULL, new_indices, 8);

    ASSERT_FLOAT_NEAR(3.0f, layout.boxes[0].door_width[0], 0.01f);
    return 0;
}

/** NarrowDoor divides door_width by 1.5. */
static int test_narrow_door_divides(void) {
    srd_rule_table_t tbl;
    register_all_feature_rules(&tbl);

    srd_sdf_layout_t layout;
    build_single_room(&layout);
    layout.boxes[0].door_width[0] = 3.0f;

    int narrow_idx = -1;
    for (int i = 0; i < tbl.n_rules; i++) {
        if (strcmp(tbl.rules[i].name, "NarrowDoor") == 0) { narrow_idx = i; break; }
    }
    ASSERT_TRUE(narrow_idx >= 0);

    srd_selection_t sel = { .indices = {0}, .n = 1 };
    int new_indices[8];
    tbl.rules[narrow_idx].apply(&layout, &sel, NULL, new_indices, 8);

    ASSERT_FLOAT_NEAR(2.0f, layout.boxes[0].door_width[0], 0.01f);
    return 0;
}

/** NarrowDoor cond rejects if door_width <= SRD_EPSILON. */
static int test_narrow_door_cond_rejects_tiny(void) {
    srd_rule_table_t tbl;
    register_all_feature_rules(&tbl);

    srd_sdf_layout_t layout;
    build_single_room(&layout);
    layout.boxes[0].door_width[0] = SRD_EPSILON;

    int narrow_idx = -1;
    for (int i = 0; i < tbl.n_rules; i++) {
        if (strcmp(tbl.rules[i].name, "NarrowDoor") == 0) { narrow_idx = i; break; }
    }

    srd_selection_t sel = { .indices = {0}, .n = 1 };
    ASSERT_TRUE(!tbl.rules[narrow_idx].cond(&layout, &sel, NULL));
    return 0;
}

/* ── Stair tests (Rules 35-38) ─────────────────────────────────────── */

/** AddStairUp inserts an epsilon-sized STAIR_UP box adjacent to sel[0]. */
static int test_add_stair_up_creates_box(void) {
    srd_rule_table_t tbl;
    register_all_feature_rules(&tbl);

    srd_sdf_layout_t layout;
    build_single_room(&layout);

    int add_up_idx = -1;
    for (int i = 0; i < tbl.n_rules; i++) {
        if (strcmp(tbl.rules[i].name, "AddStairUp") == 0) { add_up_idx = i; break; }
    }
    ASSERT_TRUE(add_up_idx >= 0);

    srd_selection_t sel = { .indices = {0}, .n = 1 };
    int new_indices[8];
    int rc = tbl.rules[add_up_idx].apply(&layout, &sel, NULL, new_indices, 8);
    ASSERT_TRUE(rc >= 0);
    ASSERT_INT_EQ(2, layout.n_boxes);

    /* New box should be STAIR_UP type */
    int new_idx = new_indices[0];
    ASSERT_INT_EQ(SRD_ROOM_STAIR_UP, layout.boxes[new_idx].type);
    /* Epsilon-sized */
    ASSERT_FLOAT_NEAR(SRD_EPSILON, layout.boxes[new_idx].hw, 0.001f);
    ASSERT_FLOAT_NEAR(SRD_EPSILON, layout.boxes[new_idx].hd, 0.001f);
    /* Adjacent to original */
    ASSERT_TRUE(srd_sdf_layout_get_adj(&layout, 0, new_idx));
    return 0;
}

/** AddStairDown inserts a STAIR_DOWN box. */
static int test_add_stair_down_creates_box(void) {
    srd_rule_table_t tbl;
    register_all_feature_rules(&tbl);

    srd_sdf_layout_t layout;
    build_single_room(&layout);

    int add_down_idx = -1;
    for (int i = 0; i < tbl.n_rules; i++) {
        if (strcmp(tbl.rules[i].name, "AddStairDown") == 0) { add_down_idx = i; break; }
    }
    ASSERT_TRUE(add_down_idx >= 0);

    srd_selection_t sel = { .indices = {0}, .n = 1 };
    int new_indices[8];
    int rc = tbl.rules[add_down_idx].apply(&layout, &sel, NULL, new_indices, 8);
    ASSERT_TRUE(rc >= 0);
    ASSERT_INT_EQ(2, layout.n_boxes);

    int new_idx = new_indices[0];
    ASSERT_INT_EQ(SRD_ROOM_STAIR_DOWN, layout.boxes[new_idx].type);
    ASSERT_FLOAT_NEAR(SRD_EPSILON, layout.boxes[new_idx].hw, 0.001f);
    return 0;
}

/** RemoveStair removes a stair box. */
static int test_remove_stair_removes_box(void) {
    srd_rule_table_t tbl;
    register_all_feature_rules(&tbl);

    srd_sdf_layout_t layout;
    build_single_room(&layout);

    /* Add a stair first */
    int add_up_idx = -1, rem_idx = -1;
    for (int i = 0; i < tbl.n_rules; i++) {
        if (strcmp(tbl.rules[i].name, "AddStairUp") == 0) add_up_idx = i;
        if (strcmp(tbl.rules[i].name, "RemoveStair") == 0) rem_idx = i;
    }

    srd_selection_t sel = { .indices = {0}, .n = 1 };
    int new_indices[8];
    tbl.rules[add_up_idx].apply(&layout, &sel, NULL, new_indices, 8);
    ASSERT_INT_EQ(2, layout.n_boxes);

    /* Now remove the stair (it's at index new_indices[0]) */
    srd_selection_t rem_sel = { .indices = {new_indices[0]}, .n = 1 };
    tbl.rules[rem_idx].apply(&layout, &rem_sel, NULL, new_indices, 8);
    ASSERT_INT_EQ(1, layout.n_boxes);
    return 0;
}

/** RemoveStair cond rejects non-stair box. */
static int test_remove_stair_cond_rejects_non_stair(void) {
    srd_rule_table_t tbl;
    register_all_feature_rules(&tbl);

    srd_sdf_layout_t layout;
    build_single_room(&layout);  /* GENERIC room */

    int rem_idx = -1;
    for (int i = 0; i < tbl.n_rules; i++) {
        if (strcmp(tbl.rules[i].name, "RemoveStair") == 0) { rem_idx = i; break; }
    }

    srd_selection_t sel = { .indices = {0}, .n = 1 };
    ASSERT_TRUE(!tbl.rules[rem_idx].cond(&layout, &sel, NULL));
    return 0;
}

/** RelocateStair moves a stair to a new host room and updates adjacency. */
static int test_relocate_stair_updates_adj(void) {
    srd_rule_table_t tbl;
    register_all_feature_rules(&tbl);

    srd_sdf_layout_t layout;
    build_two_rooms(&layout);  /* box 0 and box 1 adjacent */

    /* Add a stair adjacent to box 0 */
    int add_up_idx = -1, reloc_idx = -1;
    for (int i = 0; i < tbl.n_rules; i++) {
        if (strcmp(tbl.rules[i].name, "AddStairUp") == 0) add_up_idx = i;
        if (strcmp(tbl.rules[i].name, "RelocateStair") == 0) reloc_idx = i;
    }

    srd_selection_t sel = { .indices = {0}, .n = 1 };
    int new_indices[8];
    tbl.rules[add_up_idx].apply(&layout, &sel, NULL, new_indices, 8);
    int stair_idx = new_indices[0];
    ASSERT_INT_EQ(3, layout.n_boxes);

    /* Stair should be adjacent to box 0, not box 1 */
    ASSERT_TRUE(srd_sdf_layout_get_adj(&layout, 0, stair_idx));
    ASSERT_TRUE(!srd_sdf_layout_get_adj(&layout, 1, stair_idx));

    /* Relocate stair to box 1 — n_select=2: [stair_idx, target_i] */
    srd_selection_t reloc_sel = { .indices = {stair_idx, 1}, .n = 2 };
    int rc = tbl.rules[reloc_idx].apply(&layout, &reloc_sel, NULL, new_indices, 8);
    ASSERT_TRUE(rc >= 0);

    /* Stair should now be adjacent to box 1, not box 0 */
    ASSERT_TRUE(!srd_sdf_layout_get_adj(&layout, 0, stair_idx));
    ASSERT_TRUE(srd_sdf_layout_get_adj(&layout, 1, stair_idx));
    return 0;
}

/* ── Special room tests (Rules 39-46) ──────────────────────────────── */

/** AddDeadEnd inserts a DEAD_END box adjacent to sel[0]. */
static int test_add_dead_end_creates_box(void) {
    srd_rule_table_t tbl;
    register_all_feature_rules(&tbl);

    srd_sdf_layout_t layout;
    build_single_room(&layout);

    int add_de_idx = -1;
    for (int i = 0; i < tbl.n_rules; i++) {
        if (strcmp(tbl.rules[i].name, "AddDeadEnd") == 0) { add_de_idx = i; break; }
    }
    ASSERT_TRUE(add_de_idx >= 0);

    srd_selection_t sel = { .indices = {0}, .n = 1 };
    int new_indices[8];
    int rc = tbl.rules[add_de_idx].apply(&layout, &sel, NULL, new_indices, 8);
    ASSERT_TRUE(rc >= 0);
    ASSERT_INT_EQ(2, layout.n_boxes);

    int new_idx = new_indices[0];
    ASSERT_INT_EQ(SRD_ROOM_DEAD_END, layout.boxes[new_idx].type);
    ASSERT_TRUE(srd_sdf_layout_get_adj(&layout, 0, new_idx));
    /* Dead-end has exactly 1 neighbour */
    ASSERT_INT_EQ(1, srd_sdf_layout_adj_count(&layout, new_idx));
    return 0;
}

/** RemoveDeadEnd removes a dead-end box. */
static int test_remove_dead_end_removes_box(void) {
    srd_rule_table_t tbl;
    register_all_feature_rules(&tbl);

    srd_sdf_layout_t layout;
    build_single_room(&layout);

    int add_de_idx = -1, rem_de_idx = -1;
    for (int i = 0; i < tbl.n_rules; i++) {
        if (strcmp(tbl.rules[i].name, "AddDeadEnd") == 0) add_de_idx = i;
        if (strcmp(tbl.rules[i].name, "RemoveDeadEnd") == 0) rem_de_idx = i;
    }

    srd_selection_t sel = { .indices = {0}, .n = 1 };
    int new_indices[8];
    tbl.rules[add_de_idx].apply(&layout, &sel, NULL, new_indices, 8);
    ASSERT_INT_EQ(2, layout.n_boxes);

    srd_selection_t rem_sel = { .indices = {new_indices[0]}, .n = 1 };
    tbl.rules[rem_de_idx].apply(&layout, &rem_sel, NULL, new_indices, 8);
    ASSERT_INT_EQ(1, layout.n_boxes);
    return 0;
}

/** AddSecretRoom inserts a SECRET box. */
static int test_add_secret_room_creates_box(void) {
    srd_rule_table_t tbl;
    register_all_feature_rules(&tbl);

    srd_sdf_layout_t layout;
    build_single_room(&layout);

    int add_sec_idx = -1;
    for (int i = 0; i < tbl.n_rules; i++) {
        if (strcmp(tbl.rules[i].name, "AddSecretRoom") == 0) { add_sec_idx = i; break; }
    }
    ASSERT_TRUE(add_sec_idx >= 0);

    srd_selection_t sel = { .indices = {0}, .n = 1 };
    int new_indices[8];
    int rc = tbl.rules[add_sec_idx].apply(&layout, &sel, NULL, new_indices, 8);
    ASSERT_TRUE(rc >= 0);
    ASSERT_INT_EQ(2, layout.n_boxes);

    int new_idx = new_indices[0];
    ASSERT_INT_EQ(SRD_ROOM_SECRET, layout.boxes[new_idx].type);
    ASSERT_TRUE(srd_sdf_layout_get_adj(&layout, 0, new_idx));
    return 0;
}

/** RemoveSecretRoom cond rejects non-SECRET box. */
static int test_remove_secret_cond_rejects_non_secret(void) {
    srd_rule_table_t tbl;
    register_all_feature_rules(&tbl);

    srd_sdf_layout_t layout;
    build_single_room(&layout);  /* GENERIC */

    int rem_sec_idx = -1;
    for (int i = 0; i < tbl.n_rules; i++) {
        if (strcmp(tbl.rules[i].name, "RemoveSecretRoom") == 0) { rem_sec_idx = i; break; }
    }

    srd_selection_t sel = { .indices = {0}, .n = 1 };
    ASSERT_TRUE(!tbl.rules[rem_sec_idx].cond(&layout, &sel, NULL));
    return 0;
}

/** AddBossRoom inserts a BOSS box. */
static int test_add_boss_room_creates_box(void) {
    srd_rule_table_t tbl;
    register_all_feature_rules(&tbl);

    srd_sdf_layout_t layout;
    build_single_room(&layout);

    int add_boss_idx = -1;
    for (int i = 0; i < tbl.n_rules; i++) {
        if (strcmp(tbl.rules[i].name, "AddBossRoom") == 0) { add_boss_idx = i; break; }
    }
    ASSERT_TRUE(add_boss_idx >= 0);

    srd_selection_t sel = { .indices = {0}, .n = 1 };
    int new_indices[8];
    int rc = tbl.rules[add_boss_idx].apply(&layout, &sel, NULL, new_indices, 8);
    ASSERT_TRUE(rc >= 0);
    ASSERT_INT_EQ(2, layout.n_boxes);

    int new_idx = new_indices[0];
    ASSERT_INT_EQ(SRD_ROOM_BOSS, layout.boxes[new_idx].type);
    ASSERT_TRUE(srd_sdf_layout_get_adj(&layout, 0, new_idx));
    return 0;
}

/** AddBossRoom cond false when boss already exists. */
static int test_add_boss_room_cond_false_when_exists(void) {
    srd_rule_table_t tbl;
    register_all_feature_rules(&tbl);

    srd_sdf_layout_t layout;
    build_single_room(&layout);

    /* Pre-add a boss room box */
    srd_sdf_box_t boss = {10.0f, 10.0f, 2.0f, 2.0f, SRD_ROOM_BOSS, 0, {0}};
    srd_sdf_layout_add_box(&layout, &boss);

    int add_boss_idx = -1;
    for (int i = 0; i < tbl.n_rules; i++) {
        if (strcmp(tbl.rules[i].name, "AddBossRoom") == 0) { add_boss_idx = i; break; }
    }

    srd_selection_t sel = { .indices = {0}, .n = 1 };
    ASSERT_TRUE(!tbl.rules[add_boss_idx].cond(&layout, &sel, NULL));
    return 0;
}

/** RemoveBossRoom cond rejects non-BOSS box. */
static int test_remove_boss_cond_rejects_non_boss(void) {
    srd_rule_table_t tbl;
    register_all_feature_rules(&tbl);

    srd_sdf_layout_t layout;
    build_single_room(&layout);

    int rem_boss_idx = -1;
    for (int i = 0; i < tbl.n_rules; i++) {
        if (strcmp(tbl.rules[i].name, "RemoveBossRoom") == 0) { rem_boss_idx = i; break; }
    }

    srd_selection_t sel = { .indices = {0}, .n = 1 };
    ASSERT_TRUE(!tbl.rules[rem_boss_idx].cond(&layout, &sel, NULL));
    return 0;
}

/** AddTreasureRoom creates box with exactly 1 adjacency. */
static int test_add_treasure_room_single_neighbour(void) {
    srd_rule_table_t tbl;
    register_all_feature_rules(&tbl);

    srd_sdf_layout_t layout;
    build_single_room(&layout);

    int add_tr_idx = -1;
    for (int i = 0; i < tbl.n_rules; i++) {
        if (strcmp(tbl.rules[i].name, "AddTreasureRoom") == 0) { add_tr_idx = i; break; }
    }
    ASSERT_TRUE(add_tr_idx >= 0);

    srd_selection_t sel = { .indices = {0}, .n = 1 };
    int new_indices[8];
    int rc = tbl.rules[add_tr_idx].apply(&layout, &sel, NULL, new_indices, 8);
    ASSERT_TRUE(rc >= 0);
    ASSERT_INT_EQ(2, layout.n_boxes);

    int new_idx = new_indices[0];
    ASSERT_INT_EQ(SRD_ROOM_TREASURE, layout.boxes[new_idx].type);
    /* Treasure room should have exactly 1 neighbour */
    ASSERT_INT_EQ(1, srd_sdf_layout_adj_count(&layout, new_idx));
    return 0;
}

/** RemoveTreasureRoom removes a treasure box. */
static int test_remove_treasure_room_removes_box(void) {
    srd_rule_table_t tbl;
    register_all_feature_rules(&tbl);

    srd_sdf_layout_t layout;
    build_single_room(&layout);

    int add_tr_idx = -1, rem_tr_idx = -1;
    for (int i = 0; i < tbl.n_rules; i++) {
        if (strcmp(tbl.rules[i].name, "AddTreasureRoom") == 0) add_tr_idx = i;
        if (strcmp(tbl.rules[i].name, "RemoveTreasureRoom") == 0) rem_tr_idx = i;
    }

    srd_selection_t sel = { .indices = {0}, .n = 1 };
    int new_indices[8];
    tbl.rules[add_tr_idx].apply(&layout, &sel, NULL, new_indices, 8);
    ASSERT_INT_EQ(2, layout.n_boxes);

    srd_selection_t rem_sel = { .indices = {new_indices[0]}, .n = 1 };
    tbl.rules[rem_tr_idx].apply(&layout, &rem_sel, NULL, new_indices, 8);
    ASSERT_INT_EQ(1, layout.n_boxes);
    return 0;
}

/* ── Test runner ───────────────────────────────────────────────────── */

struct test_case {
    const char *name;
    int (*fn)(void);
};

static struct test_case TESTS[] = {
    /* Registration */
    {"register_door_count",              test_register_door_count},
    {"register_stair_count",             test_register_stair_count},
    {"register_special_count",           test_register_special_count},
    {"register_boss_count",              test_register_boss_count},
    {"register_all_16_rules",            test_register_all_16_rules},
    {"all_rules_have_names",             test_all_rules_have_names},
    {"all_rules_have_cond_and_apply",    test_all_rules_have_cond_and_apply},
    {"inverse_ids_valid",                test_inverse_ids_valid},
    /* Doors */
    {"add_door_sets_width",              test_add_door_sets_width},
    {"remove_door_clears_width",         test_remove_door_clears_width},
    {"add_remove_door_roundtrip",        test_add_remove_door_roundtrip},
    {"add_door_cond_rejects_existing",   test_add_door_cond_rejects_existing},
    {"widen_door_multiplies",            test_widen_door_multiplies},
    {"narrow_door_divides",              test_narrow_door_divides},
    {"narrow_door_cond_rejects_tiny",    test_narrow_door_cond_rejects_tiny},
    /* Stairs */
    {"add_stair_up_creates_box",         test_add_stair_up_creates_box},
    {"add_stair_down_creates_box",       test_add_stair_down_creates_box},
    {"remove_stair_removes_box",         test_remove_stair_removes_box},
    {"remove_stair_cond_rejects",        test_remove_stair_cond_rejects_non_stair},
    {"relocate_stair_updates_adj",       test_relocate_stair_updates_adj},
    /* Special rooms */
    {"add_dead_end_creates_box",         test_add_dead_end_creates_box},
    {"remove_dead_end_removes_box",      test_remove_dead_end_removes_box},
    {"add_secret_room_creates_box",      test_add_secret_room_creates_box},
    {"remove_secret_cond_rejects",       test_remove_secret_cond_rejects_non_secret},
    {"add_boss_room_creates_box",        test_add_boss_room_creates_box},
    {"add_boss_cond_false_when_exists",  test_add_boss_room_cond_false_when_exists},
    {"remove_boss_cond_rejects",         test_remove_boss_cond_rejects_non_boss},
    {"add_treasure_single_neighbour",    test_add_treasure_room_single_neighbour},
    {"remove_treasure_removes_box",      test_remove_treasure_room_removes_box},
};

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

int main(void) {
    size_t total = ARRAY_SIZE(TESTS);
    size_t passed = 0;
    for (size_t i = 0; i < total; i++) {
        struct test_case *tc = &TESTS[i];
        printf("RUN  %s\n", tc->name);
        int rc = tc->fn();
        if (rc == 0) {
            printf("  OK %s\n", tc->name);
            passed++;
        } else {
            printf("FAIL %s\n", tc->name);
        }
    }
    printf("\n%zu/%zu passed\n", passed, total);
    return (passed == total) ? 0 : 1;
}
