/**
 * @file lm_solve.c
 * @brief Progressive-refinement (Southwell) radiosity shooting (see lm_solve.h).
 */
#include "ferrum/lightmap/lm_solve.h"

#include <math.h>
#include <stddef.h>

#include "ferrum/lightmap/lm_sh.h"
#include "ferrum/lightmap/lm_visibility.h"

#define LM_SOLVE_PI 3.14159265358979324f

/* Is world point @p p inside axis-aligned @p box (inclusive)? */
static bool lm_solve_aabb_contains(const phys_aabb_t *box, vec3_t p)
{
    return p.x >= box->min.x && p.x <= box->max.x && p.y >= box->min.y &&
           p.y <= box->max.y && p.z >= box->min.z && p.z <= box->max.z;
}

/* Total unshot power of luxel i: sum of residual channels scaled by patch area. */
static float lm_solve_power(const lm_solver_t *s, uint32_t i)
{
    vec3_t r = s->residual[i];
    return (r.x + r.y + r.z) * s->area[i];
}

/* Shoot shooter s's exitance @p src into a single receiver j: form factor,
 * SVO visibility, SH deposit, and reflected residual feedback. */
static void lm_solve_shoot_to(lm_solver_t *solver,
                              const lm_solve_params_t *params, uint32_t si,
                              uint32_t ji, vec3_t src)
{
    lm_luxel_t *s = &solver->lm->luxels[si];
    lm_luxel_t *j = &solver->lm->luxels[ji];

    /* Partial-bake gate: only receivers inside the region are relit. */
    if (params->use_region && !lm_solve_aabb_contains(&params->bake_region, j->pos))
        return;

    vec3_t delta = vec3_sub(j->pos, s->pos); /* shooter -> receiver */
    float r2 = vec3_dot(delta, delta);
    if (r2 < 1e-8f)
        return;
    float inv_r = 1.0f / sqrtf(r2);
    vec3_t dir_sj = vec3_scale(delta, inv_r);

    float cos_s = vec3_dot(dir_sj, s->normal);      /* leaves shooter front */
    if (cos_s <= 0.0f)
        return;
    vec3_t dir_js = vec3_scale(dir_sj, -1.0f);      /* arrives at receiver */
    float cos_j = vec3_dot(dir_js, j->normal);
    if (cos_j <= 0.0f)
        return;

    if (solver->svo && !lm_visibility_segment(solver->svo, s->pos, j->pos))
        return; /* occluded */

    /* Projected solid angle subtended by the shooter patch at the receiver. */
    float solid_angle = cos_s * solver->area[si] / r2;
    if (solid_angle > LM_SOLVE_PI)
        solid_angle = LM_SOLVE_PI; /* guard near-coincident patches */

    /* Deposit incident radiance (= exitance/pi) into the receiver SH, and feed
     * the reflected part back into its residual for the next bounce. */
    const float *src_c = &src.x;
    float *res_c = &solver->residual[ji].x;
    const float *alb_c = &j->albedo.x;
    for (int c = 0; c < 3; ++c) {
        float radiance = src_c[c] / LM_SOLVE_PI;
        lm_sh9_add_sample(&j->sh[c], dir_js, radiance, solid_angle);
        float received = radiance * solid_angle * cos_j;
        res_c[c] += alb_c[c] * received;
    }
}

bool lm_solver_init(lm_solver_t *solver, lm_lightmap_t *lm,
                    const lm_kdtree_t *kdtree, const npc_svo_grid_t *svo,
                    const float *seed_irradiance, const float *luxel_areas,
                    float uniform_area, arena_t *arena)
{
    uint32_t n = lm->res_u * lm->res_v;
    solver->lm = lm;
    solver->kdtree = kdtree;
    solver->svo = svo;
    solver->n = n;
    solver->residual = arena_alloc(arena, _Alignof(vec3_t), n * sizeof(vec3_t));
    solver->area = arena_alloc(arena, _Alignof(float), n * sizeof(float));
    solver->scratch = arena_alloc(arena, _Alignof(uint32_t), n * sizeof(uint32_t));
    if (!solver->residual || !solver->area || !solver->scratch)
        return false;

    for (uint32_t i = 0; i < n; ++i) {
        lm_luxel_t *lx = &lm->luxels[i];
        const float *alb = &lx->albedo.x;
        float *res = &solver->residual[i].x;
        for (int c = 0; c < 3; ++c) {
            /* Area-light direct already sits in the SH; the analytic-light seed
             * is supplied separately. Both are reflected by albedo to become the
             * luxel's initial unshot exitance. */
            float e_area = lm_sh9_irradiance(&lx->sh[c], lx->normal);
            float e_seed = seed_irradiance ? seed_irradiance[i * 3 + c] : 0.0f;
            res[c] = alb[c] * (e_area + e_seed);
        }
        solver->area[i] = luxel_areas ? luxel_areas[i] : uniform_area;
    }
    return true;
}

bool lm_solver_shoot_once(lm_solver_t *solver, const lm_solve_params_t *params)
{
    /* Southwell: pick the patch with the greatest unshot power. */
    uint32_t best = 0;
    float best_power = -1.0f;
    for (uint32_t i = 0; i < solver->n; ++i) {
        float p = lm_solve_power(solver, i);
        if (p > best_power) {
            best_power = p;
            best = i;
        }
    }
    if (best_power <= params->residual_epsilon)
        return false; /* converged */

    /* Consume the shooter's residual, then distribute it to near receivers. */
    vec3_t src = solver->residual[best];
    solver->residual[best] = (vec3_t){ 0.0f, 0.0f, 0.0f };

    uint32_t found = lm_kdtree_radius(solver->kdtree, solver->lm->luxels[best].pos,
                                      params->near_radius, solver->scratch,
                                      solver->n);
    uint32_t count = found < solver->n ? found : solver->n;
    for (uint32_t k = 0; k < count; ++k) {
        uint32_t ji = solver->scratch[k];
        if (ji == best)
            continue;
        lm_solve_shoot_to(solver, params, best, ji, src);
    }
    return true;
}

uint32_t lm_solver_run(lm_solver_t *solver, const lm_solve_params_t *params)
{
    uint32_t shots = 0;
    while (shots < params->max_shots && lm_solver_shoot_once(solver, params))
        ++shots;
    return shots;
}
