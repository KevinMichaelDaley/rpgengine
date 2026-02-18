/**
 * @file snapshot_interp.c
 * @brief Whole-world snapshot interpolation — feeds decoded snapshots
 *        into per-body pose interpolators for smooth client rendering.
 *
 * Non-static functions (4): create, destroy, push, sample.
 */

#include "ferrum/net/replication/interp/snapshot_interp.h"
#include "ferrum/physics/snapshot.h"

#include <stdlib.h>
#include <string.h>

/* Dequantization scale: 1/1000 = mm → meters. */
#define POS_VEL_INV_SCALE (1.0f / 1000.0f)

/* ── create / destroy ───────────────────────────────────────── */

fr_snapshot_interp_t *fr_snapshot_interp_create(
    const fr_snapshot_interp_config_t *cfg)
{
    if (!cfg || cfg->max_bodies == 0) return NULL;

    fr_snapshot_interp_t *si = calloc(1, sizeof(*si));
    if (!si) return NULL;

    si->interps = calloc(cfg->max_bodies, sizeof(fr_pose_interpolator_t));
    if (!si->interps) {
        free(si);
        return NULL;
    }

    si->max_bodies    = cfg->max_bodies;
    si->quat_epsilon  = (cfg->quat_epsilon > 0.0f)
                        ? cfg->quat_epsilon : 1e-6f;
    si->last_recv_time = 0.0;
    si->last_tick      = 0;
    return si;
}

void fr_snapshot_interp_destroy(fr_snapshot_interp_t *si)
{
    if (!si) return;
    free(si->interps);
    free(si);
}

/* ── push ───────────────────────────────────────────────────── */

uint32_t fr_snapshot_interp_push(fr_snapshot_interp_t *si,
                                 const struct phys_snapshot *snapshot,
                                 double recv_time_s)
{
    if (!si || !snapshot || !snapshot->bodies) return 0;

    /* Drop stale/duplicate snapshots. */
    if (snapshot->tick <= si->last_tick) return 0;

    si->last_tick      = snapshot->tick;
    si->last_recv_time = recv_time_s;

    uint32_t count = snapshot->body_count;
    if (count > si->max_bodies) count = si->max_bodies;

    uint32_t updated = 0;
    for (uint32_t i = 0; i < count; i++) {
        const phys_snapshot_body_t *sb = &snapshot->bodies[i];

        /* Dequantize pose and velocities. */
        vec3_t pos = phys_dequantize_vec3(sb->position,   POS_VEL_INV_SCALE);
        quat_t rot = phys_dequantize_quat(sb->orientation);
        vec3_t vel = phys_dequantize_vec3(sb->linear_vel,  POS_VEL_INV_SCALE);
        vec3_t ang = phys_dequantize_vec3(sb->angular_vel, POS_VEL_INV_SCALE);

        /* Use server tick as server_time_s (monotonic, tick-based). */
        double server_time = (double)snapshot->tick / 60.0;

        fr_pose_interpolator_push(&si->interps[i],
                                  recv_time_s,
                                  pos, rot, vel, ang,
                                  server_time);
        updated++;
    }
    return updated;
}

/* ── sample ─────────────────────────────────────────────────── */

bool fr_snapshot_interp_sample(const fr_snapshot_interp_t *si,
                               uint32_t body_idx,
                               double now_s,
                               vec3_t *out_pos,
                               quat_t *out_rot)
{
    if (!si || !out_pos || !out_rot) return false;
    if (body_idx >= si->max_bodies)  return false;

    return fr_pose_interpolator_sample(&si->interps[body_idx],
                                       now_s,
                                       si->quat_epsilon,
                                       out_pos,
                                       out_rot);
}
