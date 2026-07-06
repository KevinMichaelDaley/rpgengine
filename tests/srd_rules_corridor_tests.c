/**
 * @file srd_rules_corridor_tests.c
 * @brief Tests for corridor/connection rules (Rules 17-30).
 *
 * Covers: registration counts, cond predicates, apply effects, roundtrips,
 * BFS-based graph queries, jump continuity (epsilon spawns), and inverse IDs.
 */
#include "ferrum/procgen/srd/srd_rules_corridor.h"
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

/* ── Helpers ───────────────────────────────────────────────────────── */

/**
 * Register all corridor rules and return total count.
 */
static int register_all_corridor_rules(srd_rule_table_t *tbl) {
    srd_rule_table_init(tbl);
    int r1 = srd_rules_corridor_register_basic(tbl);
    int r2 = srd_rules_corridor_register_shape(tbl);
    int r3 = srd_rules_corridor_register_graph(tbl);
    if (r1 < 0 || r2 < 0 || r3 < 0) return -1;
    return r1 + r2 + r3;
}

/**
 * Build layout with two rooms that are adjacent (connected).
 * Room 0: cx=3, cz=5, hw=2, hd=2
 * Room 1: cx=9, cz=5, hw=2, hd=2
 * Adjacent to each other.
 */
static void build_two_connected_rooms(srd_sdf_layout_t *layout) {
    srd_sdf_layout_init(layout);
    layout->bounds_w = 20.0f;
    layout->bounds_h = 20.0f;
    srd_sdf_box_t a = {3.0f, 5.0f, 2.0f, 2.0f, SRD_ROOM_GENERIC, 0, {0}};
    srd_sdf_box_t b = {9.0f, 5.0f, 2.0f, 2.0f, SRD_ROOM_GENERIC, 0, {0}};
    srd_sdf_layout_add_box(layout, &a);
    srd_sdf_layout_add_box(layout, &b);
    srd_sdf_layout_set_adj(layout, 0, 1, true);
}

/**
 * Build layout with two rooms that are NOT adjacent (disconnected).
 * Room 0: cx=3, cz=5
 * Room 1: cx=15, cz=15
 */
static void build_two_disconnected_rooms(srd_sdf_layout_t *layout) {
    srd_sdf_layout_init(layout);
    layout->bounds_w = 30.0f;
    layout->bounds_h = 30.0f;
    srd_sdf_box_t a = {3.0f, 5.0f, 2.0f, 2.0f, SRD_ROOM_GENERIC, 0, {0}};
    srd_sdf_box_t b = {15.0f, 15.0f, 2.0f, 2.0f, SRD_ROOM_GENERIC, 0, {0}};
    srd_sdf_layout_add_box(layout, &a);
    srd_sdf_layout_add_box(layout, &b);
    /* No adjacency set => disconnected */
}

/**
 * Build layout with a corridor between two rooms.
 * Room 0: cx=3, cz=5
 * Corridor 1: cx=6, cz=5 (type=CORRIDOR)
 * Room 2: cx=9, cz=5
 * adj[0][1]=true, adj[1][2]=true
 */
static void build_rooms_with_corridor(srd_sdf_layout_t *layout) {
    srd_sdf_layout_init(layout);
    layout->bounds_w = 20.0f;
    layout->bounds_h = 20.0f;
    srd_sdf_box_t a = {3.0f, 5.0f, 2.0f, 2.0f, SRD_ROOM_GENERIC, 0, {0}};
    srd_sdf_box_t c = {6.0f, 5.0f, 1.0f, 0.5f, SRD_ROOM_CORRIDOR, 0, {0}};
    srd_sdf_box_t b = {9.0f, 5.0f, 2.0f, 2.0f, SRD_ROOM_GENERIC, 0, {0}};
    srd_sdf_layout_add_box(layout, &a);
    srd_sdf_layout_add_box(layout, &c);
    srd_sdf_layout_add_box(layout, &b);
    srd_sdf_layout_set_adj(layout, 0, 1, true);
    srd_sdf_layout_set_adj(layout, 1, 2, true);
}

/**
 * Build a chain of 4 rooms connected linearly: 0-1-2-3
 * For ShortcutPath testing (path from 0 to 3 is 3 hops > 2).
 */
static void build_long_chain(srd_sdf_layout_t *layout) {
    srd_sdf_layout_init(layout);
    layout->bounds_w = 30.0f;
    layout->bounds_h = 30.0f;
    srd_sdf_box_t boxes[4] = {
        {3.0f,  5.0f, 2.0f, 2.0f, SRD_ROOM_GENERIC, 0, {0}},
        {9.0f,  5.0f, 2.0f, 2.0f, SRD_ROOM_GENERIC, 0, {0}},
        {15.0f, 5.0f, 2.0f, 2.0f, SRD_ROOM_GENERIC, 0, {0}},
        {21.0f, 5.0f, 2.0f, 2.0f, SRD_ROOM_GENERIC, 0, {0}},
    };
    for (int i = 0; i < 4; i++) {
        srd_sdf_layout_add_box(layout, &boxes[i]);
    }
    srd_sdf_layout_set_adj(layout, 0, 1, true);
    srd_sdf_layout_set_adj(layout, 1, 2, true);
    srd_sdf_layout_set_adj(layout, 2, 3, true);
}

/* ── Registration tests ────────────────────────────────────────────── */

static int test_register_basic_count(void) {
    srd_rule_table_t tbl;
    srd_rule_table_init(&tbl);
    int r = srd_rules_corridor_register_basic(&tbl);
    ASSERT_INT_EQ(4, r);
    ASSERT_INT_EQ(4, tbl.n_rules);
    return 0;
}

static int test_register_shape_count(void) {
    srd_rule_table_t tbl;
    srd_rule_table_init(&tbl);
    int r = srd_rules_corridor_register_shape(&tbl);
    ASSERT_INT_EQ(4, r);
    ASSERT_INT_EQ(4, tbl.n_rules);
    return 0;
}

static int test_register_graph_count(void) {
    srd_rule_table_t tbl;
    srd_rule_table_init(&tbl);
    int r = srd_rules_corridor_register_graph(&tbl);
    ASSERT_INT_EQ(6, r);
    ASSERT_INT_EQ(6, tbl.n_rules);
    return 0;
}

static int test_register_all_14_rules(void) {
    srd_rule_table_t tbl;
    int total = register_all_corridor_rules(&tbl);
    ASSERT_INT_EQ(14, total);
    ASSERT_INT_EQ(14, tbl.n_rules);
    return 0;
}

static int test_all_rules_have_names(void) {
    srd_rule_table_t tbl;
    register_all_corridor_rules(&tbl);
    for (int i = 0; i < tbl.n_rules; i++) {
        ASSERT_TRUE(tbl.rules[i].name != NULL);
        ASSERT_TRUE(tbl.rules[i].name[0] != '\0');
    }
    return 0;
}

static int test_all_rules_have_cond_and_apply(void) {
    srd_rule_table_t tbl;
    register_all_corridor_rules(&tbl);
    for (int i = 0; i < tbl.n_rules; i++) {
        ASSERT_TRUE(tbl.rules[i].cond != NULL);
        ASSERT_TRUE(tbl.rules[i].apply != NULL);
    }
    return 0;
}

static int test_inverse_ids_valid(void) {
    srd_rule_table_t tbl;
    register_all_corridor_rules(&tbl);
    for (int i = 0; i < tbl.n_rules; i++) {
        int inv = tbl.rules[i].inverse_rule_id;
        if (inv >= 0) {
            ASSERT_TRUE(inv < tbl.n_rules);
            ASSERT_TRUE(tbl.rules[inv].name != NULL);
        }
    }
    return 0;
}

/* ── AddCorridor (Rule 17) ─────────────────────────────────────────── */

static int test_add_corridor_spawns_epsilon(void) {
    srd_rule_table_t tbl;
    register_all_corridor_rules(&tbl);
    /* AddCorridor is registered after RemoveCorridor.
     * Basic group: RemoveCorridor=0, AddCorridor=1, Widen=2(?), ...
     * But actually: RemoveCorridor first, then AddCorridor, then NarrowCorridor, then WidenCorridor.
     * Let's find AddCorridor by name. */
    int add_idx = -1;
    for (int i = 0; i < tbl.n_rules; i++) {
        if (strcmp(tbl.rules[i].name, "AddCorridor") == 0) {
            add_idx = i;
            break;
        }
    }
    ASSERT_TRUE(add_idx >= 0);

    srd_sdf_layout_t layout;
    build_two_connected_rooms(&layout);
    /* sel: two boxes to connect with a corridor */
    srd_selection_t sel = { .indices = {0, 1}, .n = 2 };

    int new_indices[8];
    int n_new = tbl.rules[add_idx].apply(&layout, &sel, NULL, new_indices, 8);
    ASSERT_TRUE(n_new >= 0);
    ASSERT_INT_EQ(3, layout.n_boxes);

    /* New corridor box should be at epsilon size */
    int k = new_indices[0];
    ASSERT_FLOAT_NEAR(SRD_EPSILON, layout.boxes[k].hw, 0.001f);
    ASSERT_FLOAT_NEAR(SRD_EPSILON, layout.boxes[k].hd, 0.001f);
    ASSERT_INT_EQ(SRD_ROOM_CORRIDOR, layout.boxes[k].type);

    /* Centre at midpoint of rooms 0 and 1 */
    ASSERT_FLOAT_NEAR(6.0f, layout.boxes[k].cx, 0.01f);
    ASSERT_FLOAT_NEAR(5.0f, layout.boxes[k].cz, 0.01f);

    /* Adjacent to both rooms */
    ASSERT_TRUE(srd_sdf_layout_get_adj(&layout, 0, k));
    ASSERT_TRUE(srd_sdf_layout_get_adj(&layout, 1, k));
    return 0;
}

/* ── AddCorridor + RemoveCorridor roundtrip ────────────────────────── */

static int test_add_remove_corridor_roundtrip(void) {
    srd_rule_table_t tbl;
    register_all_corridor_rules(&tbl);
    int add_idx = -1, rem_idx = -1;
    for (int i = 0; i < tbl.n_rules; i++) {
        if (strcmp(tbl.rules[i].name, "AddCorridor") == 0) add_idx = i;
        if (strcmp(tbl.rules[i].name, "RemoveCorridor") == 0) rem_idx = i;
    }
    ASSERT_TRUE(add_idx >= 0);
    ASSERT_TRUE(rem_idx >= 0);

    srd_sdf_layout_t layout;
    build_two_connected_rooms(&layout);
    ASSERT_INT_EQ(2, layout.n_boxes);

    /* Add corridor between rooms 0 and 1 */
    srd_selection_t sel_add = { .indices = {0, 1}, .n = 2 };
    int new_indices[8];
    tbl.rules[add_idx].apply(&layout, &sel_add, NULL, new_indices, 8);
    ASSERT_INT_EQ(3, layout.n_boxes);
    int k = new_indices[0];

    /* Remove the corridor */
    srd_selection_t sel_rem = { .indices = {k}, .n = 1 };
    tbl.rules[rem_idx].apply(&layout, &sel_rem, NULL, new_indices, 8);
    ASSERT_INT_EQ(2, layout.n_boxes);

    /* Original rooms should still exist and be adjacent to each other */
    ASSERT_TRUE(srd_sdf_layout_get_adj(&layout, 0, 1));
    return 0;
}

/* ── RemoveCorridor cond checks type ───────────────────────────────── */

static int test_remove_corridor_rejects_non_corridor(void) {
    srd_rule_table_t tbl;
    register_all_corridor_rules(&tbl);
    int rem_idx = -1;
    for (int i = 0; i < tbl.n_rules; i++) {
        if (strcmp(tbl.rules[i].name, "RemoveCorridor") == 0) {
            rem_idx = i;
            break;
        }
    }
    ASSERT_TRUE(rem_idx >= 0);

    srd_sdf_layout_t layout;
    build_two_connected_rooms(&layout);
    /* Room 0 is GENERIC, not CORRIDOR */
    srd_selection_t sel = { .indices = {0}, .n = 1 };
    ASSERT_TRUE(!tbl.rules[rem_idx].cond(&layout, &sel, NULL));
    return 0;
}

/* ── WidenCorridor / NarrowCorridor ────────────────────────────────── */

static int test_widen_narrow_roundtrip(void) {
    srd_rule_table_t tbl;
    register_all_corridor_rules(&tbl);
    int widen_idx = -1, narrow_idx = -1;
    for (int i = 0; i < tbl.n_rules; i++) {
        if (strcmp(tbl.rules[i].name, "WidenCorridor") == 0) widen_idx = i;
        if (strcmp(tbl.rules[i].name, "NarrowCorridor") == 0) narrow_idx = i;
    }
    ASSERT_TRUE(widen_idx >= 0);
    ASSERT_TRUE(narrow_idx >= 0);

    srd_sdf_layout_t layout;
    build_rooms_with_corridor(&layout);
    /* Corridor is box 1, hw=1.0, hd=0.5 */
    float orig_hw = layout.boxes[1].hw;
    float orig_hd = layout.boxes[1].hd;

    /* Widen */
    srd_selection_t sel = { .indices = {1}, .n = 1 };
    int new_indices[8];
    tbl.rules[widen_idx].apply(&layout, &sel, NULL, new_indices, 8);
    ASSERT_FLOAT_NEAR(orig_hw * 1.5f, layout.boxes[1].hw, 0.001f);
    ASSERT_FLOAT_NEAR(orig_hd * 1.5f, layout.boxes[1].hd, 0.001f);

    /* Narrow back */
    tbl.rules[narrow_idx].apply(&layout, &sel, NULL, new_indices, 8);
    ASSERT_FLOAT_NEAR(orig_hw, layout.boxes[1].hw, 0.001f);
    ASSERT_FLOAT_NEAR(orig_hd, layout.boxes[1].hd, 0.001f);
    return 0;
}

static int test_narrow_rejects_tiny_corridor(void) {
    srd_rule_table_t tbl;
    register_all_corridor_rules(&tbl);
    int narrow_idx = -1;
    for (int i = 0; i < tbl.n_rules; i++) {
        if (strcmp(tbl.rules[i].name, "NarrowCorridor") == 0) {
            narrow_idx = i;
            break;
        }
    }
    ASSERT_TRUE(narrow_idx >= 0);

    srd_sdf_layout_t layout;
    srd_sdf_layout_init(&layout);
    /* Corridor at epsilon size - too small to narrow */
    srd_sdf_box_t c = {5.0f, 5.0f, SRD_EPSILON, SRD_EPSILON,
                       SRD_ROOM_CORRIDOR, 0, {0}};
    srd_sdf_layout_add_box(&layout, &c);

    srd_selection_t sel = { .indices = {0}, .n = 1 };
    ASSERT_TRUE(!tbl.rules[narrow_idx].cond(&layout, &sel, NULL));
    return 0;
}

/* ── BridgeComponents ──────────────────────────────────────────────── */

static int test_bridge_cond_true_when_disconnected(void) {
    srd_rule_table_t tbl;
    register_all_corridor_rules(&tbl);
    int bridge_idx = -1;
    for (int i = 0; i < tbl.n_rules; i++) {
        if (strcmp(tbl.rules[i].name, "BridgeComponents") == 0) {
            bridge_idx = i;
            break;
        }
    }
    ASSERT_TRUE(bridge_idx >= 0);

    srd_sdf_layout_t layout;
    build_two_disconnected_rooms(&layout);

    srd_selection_t sel = { .indices = {0, 1}, .n = 2 };
    ASSERT_TRUE(tbl.rules[bridge_idx].cond(&layout, &sel, NULL));
    return 0;
}

static int test_bridge_cond_false_when_connected(void) {
    srd_rule_table_t tbl;
    register_all_corridor_rules(&tbl);
    int bridge_idx = -1;
    for (int i = 0; i < tbl.n_rules; i++) {
        if (strcmp(tbl.rules[i].name, "BridgeComponents") == 0) {
            bridge_idx = i;
            break;
        }
    }
    ASSERT_TRUE(bridge_idx >= 0);

    srd_sdf_layout_t layout;
    build_two_connected_rooms(&layout);  /* Already adjacent */

    srd_selection_t sel = { .indices = {0, 1}, .n = 2 };
    ASSERT_TRUE(!tbl.rules[bridge_idx].cond(&layout, &sel, NULL));
    return 0;
}

static int test_bridge_apply_connects_components(void) {
    srd_rule_table_t tbl;
    register_all_corridor_rules(&tbl);
    int bridge_idx = -1;
    for (int i = 0; i < tbl.n_rules; i++) {
        if (strcmp(tbl.rules[i].name, "BridgeComponents") == 0) {
            bridge_idx = i;
            break;
        }
    }
    ASSERT_TRUE(bridge_idx >= 0);

    srd_sdf_layout_t layout;
    build_two_disconnected_rooms(&layout);

    srd_selection_t sel = { .indices = {0, 1}, .n = 2 };
    int new_indices[8];
    int n_new = tbl.rules[bridge_idx].apply(&layout, &sel, NULL, new_indices, 8);
    ASSERT_TRUE(n_new >= 0);
    ASSERT_INT_EQ(3, layout.n_boxes);

    /* The new corridor should connect both rooms */
    int k = new_indices[0];
    ASSERT_TRUE(srd_sdf_layout_get_adj(&layout, 0, k));
    ASSERT_TRUE(srd_sdf_layout_get_adj(&layout, 1, k));
    ASSERT_INT_EQ(SRD_ROOM_CORRIDOR, layout.boxes[k].type);
    return 0;
}

/* ── AddLoop ───────────────────────────────────────────────────────── */

static int test_add_loop_cond_true_same_component_not_adjacent(void) {
    srd_rule_table_t tbl;
    register_all_corridor_rules(&tbl);
    int loop_idx = -1;
    for (int i = 0; i < tbl.n_rules; i++) {
        if (strcmp(tbl.rules[i].name, "AddLoop") == 0) {
            loop_idx = i;
            break;
        }
    }
    ASSERT_TRUE(loop_idx >= 0);

    /* Chain: 0-1-2. Rooms 0 and 2 are in same component but not adjacent. */
    srd_sdf_layout_t layout;
    srd_sdf_layout_init(&layout);
    layout.bounds_w = 20.0f;
    layout.bounds_h = 20.0f;
    srd_sdf_box_t boxes[3] = {
        {3.0f, 5.0f, 2.0f, 2.0f, SRD_ROOM_GENERIC, 0, {0}},
        {9.0f, 5.0f, 2.0f, 2.0f, SRD_ROOM_GENERIC, 0, {0}},
        {15.0f, 5.0f, 2.0f, 2.0f, SRD_ROOM_GENERIC, 0, {0}},
    };
    for (int i = 0; i < 3; i++) srd_sdf_layout_add_box(&layout, &boxes[i]);
    srd_sdf_layout_set_adj(&layout, 0, 1, true);
    srd_sdf_layout_set_adj(&layout, 1, 2, true);

    srd_selection_t sel = { .indices = {0, 2}, .n = 2 };
    ASSERT_TRUE(tbl.rules[loop_idx].cond(&layout, &sel, NULL));
    return 0;
}

static int test_add_loop_cond_false_when_already_adjacent(void) {
    srd_rule_table_t tbl;
    register_all_corridor_rules(&tbl);
    int loop_idx = -1;
    for (int i = 0; i < tbl.n_rules; i++) {
        if (strcmp(tbl.rules[i].name, "AddLoop") == 0) {
            loop_idx = i;
            break;
        }
    }
    ASSERT_TRUE(loop_idx >= 0);

    srd_sdf_layout_t layout;
    build_two_connected_rooms(&layout);

    srd_selection_t sel = { .indices = {0, 1}, .n = 2 };
    ASSERT_TRUE(!tbl.rules[loop_idx].cond(&layout, &sel, NULL));
    return 0;
}

/* ── RemoveLoop ────────────────────────────────────────────────────── */

static int test_remove_loop_rejected_when_disconnects(void) {
    srd_rule_table_t tbl;
    register_all_corridor_rules(&tbl);
    int rem_loop_idx = -1;
    for (int i = 0; i < tbl.n_rules; i++) {
        if (strcmp(tbl.rules[i].name, "RemoveLoop") == 0) {
            rem_loop_idx = i;
            break;
        }
    }
    ASSERT_TRUE(rem_loop_idx >= 0);

    /* Layout: room0 - corridor1 - room2. Removing corridor1 disconnects. */
    srd_sdf_layout_t layout;
    build_rooms_with_corridor(&layout);

    srd_selection_t sel = { .indices = {1}, .n = 1 };
    ASSERT_TRUE(!tbl.rules[rem_loop_idx].cond(&layout, &sel, NULL));
    return 0;
}

static int test_remove_loop_accepted_when_stays_connected(void) {
    srd_rule_table_t tbl;
    register_all_corridor_rules(&tbl);
    int rem_loop_idx = -1;
    for (int i = 0; i < tbl.n_rules; i++) {
        if (strcmp(tbl.rules[i].name, "RemoveLoop") == 0) {
            rem_loop_idx = i;
            break;
        }
    }
    ASSERT_TRUE(rem_loop_idx >= 0);

    /* Layout: room0 - corridor1 - room2, AND room0 directly adj to room2.
     * Removing corridor1 keeps graph connected via direct 0-2 edge. */
    srd_sdf_layout_t layout;
    build_rooms_with_corridor(&layout);
    srd_sdf_layout_set_adj(&layout, 0, 2, true);  /* Direct link */

    srd_selection_t sel = { .indices = {1}, .n = 1 };
    ASSERT_TRUE(tbl.rules[rem_loop_idx].cond(&layout, &sel, NULL));
    return 0;
}

/* ── ShortcutPath ──────────────────────────────────────────────────── */

static int test_shortcut_cond_true_long_path(void) {
    srd_rule_table_t tbl;
    register_all_corridor_rules(&tbl);
    int sc_idx = -1;
    for (int i = 0; i < tbl.n_rules; i++) {
        if (strcmp(tbl.rules[i].name, "ShortcutPath") == 0) {
            sc_idx = i;
            break;
        }
    }
    ASSERT_TRUE(sc_idx >= 0);

    srd_sdf_layout_t layout;
    build_long_chain(&layout);  /* 0-1-2-3, path 0 to 3 is 3 hops */

    srd_selection_t sel = { .indices = {0, 3}, .n = 2 };
    ASSERT_TRUE(tbl.rules[sc_idx].cond(&layout, &sel, NULL));
    return 0;
}

static int test_shortcut_cond_false_short_path(void) {
    srd_rule_table_t tbl;
    register_all_corridor_rules(&tbl);
    int sc_idx = -1;
    for (int i = 0; i < tbl.n_rules; i++) {
        if (strcmp(tbl.rules[i].name, "ShortcutPath") == 0) {
            sc_idx = i;
            break;
        }
    }
    ASSERT_TRUE(sc_idx >= 0);

    srd_sdf_layout_t layout;
    build_long_chain(&layout);

    /* Path from 0 to 1 is 1 hop => not > 2 */
    srd_selection_t sel = { .indices = {0, 1}, .n = 2 };
    ASSERT_TRUE(!tbl.rules[sc_idx].cond(&layout, &sel, NULL));
    return 0;
}

/* ── RerouteCorridor ───────────────────────────────────────────────── */

static int test_reroute_updates_adj_and_centre(void) {
    srd_rule_table_t tbl;
    register_all_corridor_rules(&tbl);
    int reroute_idx = -1;
    for (int i = 0; i < tbl.n_rules; i++) {
        if (strcmp(tbl.rules[i].name, "RerouteCorridor") == 0) {
            reroute_idx = i;
            break;
        }
    }
    ASSERT_TRUE(reroute_idx >= 0);

    /* Layout: room0, corridor1, room2, room3.
     * corridor1 connects room0 and room2. Reroute to room3. */
    srd_sdf_layout_t layout;
    srd_sdf_layout_init(&layout);
    layout.bounds_w = 30.0f;
    layout.bounds_h = 30.0f;
    srd_sdf_box_t r0 = {3.0f, 5.0f, 2.0f, 2.0f, SRD_ROOM_GENERIC, 0, {0}};
    srd_sdf_box_t c1 = {6.0f, 5.0f, 0.5f, 0.5f, SRD_ROOM_CORRIDOR, 0, {0}};
    srd_sdf_box_t r2 = {9.0f, 5.0f, 2.0f, 2.0f, SRD_ROOM_GENERIC, 0, {0}};
    srd_sdf_box_t r3 = {9.0f, 15.0f, 2.0f, 2.0f, SRD_ROOM_GENERIC, 0, {0}};
    srd_sdf_layout_add_box(&layout, &r0);
    srd_sdf_layout_add_box(&layout, &c1);
    srd_sdf_layout_add_box(&layout, &r2);
    srd_sdf_layout_add_box(&layout, &r3);
    srd_sdf_layout_set_adj(&layout, 0, 1, true);
    srd_sdf_layout_set_adj(&layout, 1, 2, true);

    /* sel: corridor=1, new_j=3 */
    srd_selection_t sel = { .indices = {1, 3}, .n = 2 };
    int new_indices[8];
    tbl.rules[reroute_idx].apply(&layout, &sel, NULL, new_indices, 8);

    /* Corridor should now be adjacent to room0 and room3, NOT room2 */
    ASSERT_TRUE(srd_sdf_layout_get_adj(&layout, 0, 1));
    ASSERT_TRUE(srd_sdf_layout_get_adj(&layout, 1, 3));
    ASSERT_TRUE(!srd_sdf_layout_get_adj(&layout, 1, 2));

    /* Centre should be midpoint of room0 and room3 */
    float expected_cx = (3.0f + 9.0f) * 0.5f;
    float expected_cz = (5.0f + 15.0f) * 0.5f;
    ASSERT_FLOAT_NEAR(expected_cx, layout.boxes[1].cx, 0.01f);
    ASSERT_FLOAT_NEAR(expected_cz, layout.boxes[1].cz, 0.01f);
    return 0;
}

/* ── BendCorridor / StraightenCorridor ─────────────────────────────── */

static int test_bend_straighten_roundtrip(void) {
    srd_rule_table_t tbl;
    register_all_corridor_rules(&tbl);
    int bend_idx = -1, straight_idx = -1;
    for (int i = 0; i < tbl.n_rules; i++) {
        if (strcmp(tbl.rules[i].name, "BendCorridor") == 0) bend_idx = i;
        if (strcmp(tbl.rules[i].name, "StraightenCorridor") == 0) straight_idx = i;
    }
    ASSERT_TRUE(bend_idx >= 0);
    ASSERT_TRUE(straight_idx >= 0);

    srd_sdf_layout_t layout;
    build_rooms_with_corridor(&layout);
    /* Corridor is box 1 */
    ASSERT_INT_EQ(3, layout.n_boxes);

    /* Bend the corridor */
    srd_selection_t sel_bend = { .indices = {1}, .n = 1 };
    int new_indices[8];
    int n_new = tbl.rules[bend_idx].apply(&layout, &sel_bend, NULL, new_indices, 8);
    ASSERT_TRUE(n_new >= 0);
    /* Should now have 4 boxes (2 rooms + 2 corridor segments) */
    ASSERT_INT_EQ(4, layout.n_boxes);

    /* Straighten the two corridor segments */
    /* The two new corridor segments should be adjacent to each other */
    int k1 = new_indices[0];
    int k2 = new_indices[1];
    srd_selection_t sel_straight = { .indices = {k1, k2}, .n = 2 };
    tbl.rules[straight_idx].apply(&layout, &sel_straight, NULL, new_indices, 8);
    /* Should be back to 3 boxes */
    ASSERT_INT_EQ(3, layout.n_boxes);
    return 0;
}

/* ── SplitCorridor / MergeCorridor ─────────────────────────────────── */

static int test_split_merge_corridor_roundtrip(void) {
    srd_rule_table_t tbl;
    register_all_corridor_rules(&tbl);
    int split_idx = -1, merge_idx = -1;
    for (int i = 0; i < tbl.n_rules; i++) {
        if (strcmp(tbl.rules[i].name, "SplitCorridor") == 0) split_idx = i;
        if (strcmp(tbl.rules[i].name, "MergeCorridor") == 0) merge_idx = i;
    }
    ASSERT_TRUE(split_idx >= 0);
    ASSERT_TRUE(merge_idx >= 0);

    srd_sdf_layout_t layout;
    build_rooms_with_corridor(&layout);
    ASSERT_INT_EQ(3, layout.n_boxes);

    /* Split corridor (box 1) */
    srd_selection_t sel_split = { .indices = {1}, .n = 1 };
    int new_indices[8];
    int n_new = tbl.rules[split_idx].apply(&layout, &sel_split, NULL, new_indices, 8);
    ASSERT_TRUE(n_new >= 0);
    /* Should now have 5 boxes: 2 rooms + waypoint + 2 corridor stubs */
    ASSERT_INT_EQ(5, layout.n_boxes);

    /* Merge: sel = {k1, w, k2} where k1,k2 are corridors and w is waypoint.
     * split_corridor_apply returns [0]=waypoint, [1]=corr1, [2]=corr2 */
    int w  = new_indices[0];
    int k1 = new_indices[1];
    int k2 = new_indices[2];
    srd_selection_t sel_merge = { .indices = {k1, w, k2}, .n = 3 };
    tbl.rules[merge_idx].apply(&layout, &sel_merge, NULL, new_indices, 8);
    /* Should be back to 3 boxes */
    ASSERT_INT_EQ(3, layout.n_boxes);
    return 0;
}

/* ── RemoveShortcut ────────────────────────────────────────────────── */

static int test_remove_shortcut_accepted_when_stays_connected(void) {
    srd_rule_table_t tbl;
    register_all_corridor_rules(&tbl);
    int rem_sc_idx = -1;
    for (int i = 0; i < tbl.n_rules; i++) {
        if (strcmp(tbl.rules[i].name, "RemoveShortcut") == 0) {
            rem_sc_idx = i;
            break;
        }
    }
    ASSERT_TRUE(rem_sc_idx >= 0);

    /* 0-1-2-3 chain + corridor 4 shortcutting 0-3.
     * Removing corridor 4 still leaves 0 and 3 connected. */
    srd_sdf_layout_t layout;
    build_long_chain(&layout);
    srd_sdf_box_t shortcut = {12.0f, 5.0f, SRD_EPSILON, SRD_EPSILON,
                              SRD_ROOM_CORRIDOR, 0, {0}};
    int k = srd_sdf_layout_add_box(&layout, &shortcut);
    srd_sdf_layout_set_adj(&layout, 0, k, true);
    srd_sdf_layout_set_adj(&layout, 3, k, true);

    srd_selection_t sel = { .indices = {k}, .n = 1 };
    ASSERT_TRUE(tbl.rules[rem_sc_idx].cond(&layout, &sel, NULL));
    return 0;
}

/* ── Test runner ───────────────────────────────────────────────────── */

struct test_case {
    const char *name;
    int (*fn)(void);
};

static struct test_case TESTS[] = {
    /* Registration */
    {"register_basic_count",                test_register_basic_count},
    {"register_shape_count",                test_register_shape_count},
    {"register_graph_count",                test_register_graph_count},
    {"register_all_14_rules",               test_register_all_14_rules},
    {"all_rules_have_names",                test_all_rules_have_names},
    {"all_rules_have_cond_and_apply",       test_all_rules_have_cond_and_apply},
    {"inverse_ids_valid",                   test_inverse_ids_valid},
    /* AddCorridor */
    {"add_corridor_spawns_epsilon",         test_add_corridor_spawns_epsilon},
    {"add_remove_corridor_roundtrip",       test_add_remove_corridor_roundtrip},
    /* RemoveCorridor */
    {"remove_corridor_rejects_non_corr",    test_remove_corridor_rejects_non_corridor},
    /* Widen/Narrow */
    {"widen_narrow_roundtrip",              test_widen_narrow_roundtrip},
    {"narrow_rejects_tiny_corridor",        test_narrow_rejects_tiny_corridor},
    /* BridgeComponents */
    {"bridge_cond_true_disconnected",       test_bridge_cond_true_when_disconnected},
    {"bridge_cond_false_connected",         test_bridge_cond_false_when_connected},
    {"bridge_apply_connects",               test_bridge_apply_connects_components},
    /* AddLoop */
    {"add_loop_cond_true_not_adjacent",     test_add_loop_cond_true_same_component_not_adjacent},
    {"add_loop_cond_false_adjacent",        test_add_loop_cond_false_when_already_adjacent},
    /* RemoveLoop */
    {"remove_loop_rejected_disconnects",    test_remove_loop_rejected_when_disconnects},
    {"remove_loop_accepted_stays_conn",     test_remove_loop_accepted_when_stays_connected},
    /* ShortcutPath */
    {"shortcut_cond_true_long_path",        test_shortcut_cond_true_long_path},
    {"shortcut_cond_false_short_path",      test_shortcut_cond_false_short_path},
    /* RerouteCorridor */
    {"reroute_updates_adj_and_centre",      test_reroute_updates_adj_and_centre},
    /* Bend/Straighten */
    {"bend_straighten_roundtrip",           test_bend_straighten_roundtrip},
    /* Split/Merge corridor */
    {"split_merge_corridor_roundtrip",      test_split_merge_corridor_roundtrip},
    /* RemoveShortcut */
    {"remove_shortcut_accepted_connected",  test_remove_shortcut_accepted_when_stays_connected},
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
