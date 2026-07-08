#define _POSIX_C_SOURCE 199309L

/**
 * @file srd_descent_loop.c
 * @brief Pure discrete SRD descent loop on voxel SDF grid.
 *
 * Each iteration: sample K candidate (rule, selection) pairs, evaluate
 * each by applying to a grid copy and scoring with the grid-based critic,
 * accept the best if it improves loss (or passes temperature gate),
 * anneal temperature. Terminates when time_budget_s is exhausted.
 *
 * Non-static functions (1): srd_descent_optimize
 */
#include "ferrum/procgen/srd/srd_descent_loop.h"
#include "ferrum/procgen/srd/srd_grid_critic.h"
#include "ferrum/procgen/srd/srd_voxel_rule_table.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ── RNG ──────────────────────────────────────────────────────── */

/** @brief xorshift32 PRNG. */
static uint32_t rng_next(uint32_t *state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

/** @brief Random int in [lo, hi] inclusive. */
static int rng_int_range(uint32_t *state, int lo, int hi) {
    if (lo >= hi) return lo;
    uint32_t range = (uint32_t)(hi - lo + 1);
    return lo + (int)(rng_next(state) % range);
}

/** @brief Random float in [lo, hi]. */
static float rng_float_range(uint32_t *state, float lo, float hi) {
    float t = (float)(rng_next(state) & 0xFFFF) / 65535.0f;
    return lo + t * (hi - lo);
}

/* ── Timing ───────────────────────────────────────────────────── */

static double elapsed_since(const struct timespec *t0) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (double)(now.tv_sec - t0->tv_sec) +
           (double)(now.tv_nsec - t0->tv_nsec) * 1e-9;
}

/* ── Candidate sampling ──────────────────────────────────────── */

/** @brief Wall faces for random selection. */
static const srd_face_t WALL_FACES[4] = {
    SRD_FACE_NORTH, SRD_FACE_SOUTH, SRD_FACE_EAST, SRD_FACE_WEST
};

/**
 * @brief Sample a random (rule, selection) pair.
 *
 * Picks a random rule from the table, a random room, and fills in
 * face/corner/param according to the rule's constraints.
 */
static void sample_candidate(const srd_descent_config_t *cfg,
                              const srd_room_map_t *map,
                              uint32_t *rng_state,
                              int *out_rule_idx,
                              srd_voxel_selection_t *out_sel) {
    int ri = rng_int_range(rng_state, 0, cfg->n_rules - 1);
    const srd_voxel_rule_entry_t *entry = &cfg->rules[ri];

    *out_rule_idx = ri;
    out_sel->room_id = (uint8_t)rng_int_range(rng_state, 1, map->n_rooms);
    out_sel->param = rng_float_range(rng_state, entry->param_min, entry->param_max);

    /* Face selection */
    if (entry->required_face == SRD_FACE_NONE) {
        out_sel->face = SRD_FACE_NONE;
    } else if (entry->required_face == SRD_FACE_CEIL ||
               entry->required_face == SRD_FACE_FLOOR) {
        out_sel->face = entry->required_face;
    } else {
        /* Wall face: randomise among N/S/E/W */
        out_sel->face = WALL_FACES[rng_int_range(rng_state, 0, 3)];
    }

    /* Corner selection */
    if (entry->needs_corner) {
        out_sel->corner = rng_int_range(rng_state, 0, 3);
    } else {
        out_sel->corner = -1;
    }
}

/* ── Public API ──────────────────────────────────────────────── */

srd_descent_result_t srd_descent_optimize(srd_sdf_grid_t *grid,
                                          srd_room_map_t *map,
                                          const srd_descent_config_t *cfg) {
    srd_descent_result_t result;
    memset(&result, 0, sizeof(result));

    if (!grid || !grid->values || !map || !map->ids ||
        !cfg || !cfg->rules || cfg->n_rules < 1) {
        result.final_loss = -1.0f;
        return result;
    }

    struct timespec t0;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    /* Initial loss */
    srd_grid_critic_result_t crit = srd_grid_critic_evaluate(
        grid, map, &cfg->critic_cfg);
    result.initial_loss = crit.total;
    float current_loss = crit.total;
    float temperature = cfg->temperature_init;
    int iteration = 0;

    /* RNG seed */
    uint32_t rng_state = 42u ^ (uint32_t)(cfg->time_budget_s * 1e6);
    if (rng_state == 0) rng_state = 1;

    int k = cfg->k_candidates;
    if (k < 1) k = 1;

    int stall_count = 0;

    while (elapsed_since(&t0) < cfg->time_budget_s) {
        float best_loss = current_loss;
        int best_rule = -1;
        srd_voxel_selection_t best_sel;
        memset(&best_sel, 0, sizeof(best_sel));

        /* Sample and evaluate K candidates */
        for (int c = 0; c < k; c++) {
            if (elapsed_since(&t0) >= cfg->time_budget_s) break;

            int rule_idx;
            srd_voxel_selection_t sel;
            sample_candidate(cfg, map, &rng_state, &rule_idx, &sel);

            /* Copy grid + map */
            srd_sdf_grid_t grid_copy;
            srd_room_map_t map_copy;
            if (srd_sdf_grid_copy(&grid_copy, grid) != 0) continue;
            if (srd_room_map_copy(&map_copy, map) != 0) {
                srd_sdf_grid_destroy(&grid_copy);
                continue;
            }

            /* Apply rule to copy */
            int rc = cfg->rules[rule_idx].apply(&grid_copy, &map_copy, &sel);
            if (rc != 0) {
                srd_sdf_grid_destroy(&grid_copy);
                srd_room_map_destroy(&map_copy);
                continue;
            }

            /* Evaluate copy */
            srd_grid_critic_result_t c_result = srd_grid_critic_evaluate(
                &grid_copy, &map_copy, &cfg->critic_cfg);

            if (c_result.total < best_loss) {
                best_loss = c_result.total;
                best_rule = rule_idx;
                best_sel = sel;
            }

            srd_sdf_grid_destroy(&grid_copy);
            srd_room_map_destroy(&map_copy);
        }

        /* Accept the best candidate if it improves loss, or
         * probabilistically accept worse moves at high temperature */
        if (best_rule >= 0) {
            float delta = best_loss - current_loss;
            int accept = (delta < 0.0f);

            if (!accept && temperature > cfg->temperature_min) {
                /* Metropolis acceptance: exp(-delta / T) */
                float p = expf(-delta / temperature);
                float r = rng_float_range(&rng_state, 0.0f, 1.0f);
                accept = (r < p);
            }

            if (accept) {
                cfg->rules[best_rule].apply(grid, map, &best_sel);
                current_loss = best_loss;
            }
        }

        /* Track stalls — stop early if loss hasn't improved */
        if (best_rule < 0 || best_loss >= current_loss) {
            stall_count++;
        } else {
            stall_count = 0;
        }

        /* Early exit: loss is zero or stalled for 50 iterations */
        if (current_loss <= 0.0f || stall_count >= 50) break;

        /* Anneal temperature */
        temperature *= cfg->temperature_decay;
        if (temperature < cfg->temperature_min) {
            temperature = cfg->temperature_min;
        }

        iteration++;

        /* Verbose logging */
        if (cfg->verbose && (iteration % 10 == 0)) {
            fprintf(stderr, "[descent] iter=%d loss=%.4f temp=%.4f t=%.3fs\n",
                    iteration, current_loss, temperature, elapsed_since(&t0));
        }
    }

    result.final_loss = current_loss;
    result.final_temperature = temperature;
    result.iterations = iteration;
    return result;
}
