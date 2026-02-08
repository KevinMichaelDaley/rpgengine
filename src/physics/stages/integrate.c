/**
 * @file integrate.c
 * @brief Stage 12: Integrate + Sleep.
 *
 * Updates body positions/orientations from solved velocities, applies
 * gravity, and detects sleeping bodies based on velocity thresholds.
 */

#include "ferrum/physics/integrate.h"

#include <math.h>
#include <stddef.h>

#include "ferrum/math/quat.h"
#include "ferrum/math/vec3.h"
#include "ferrum/physics/body.h"
#include "ferrum/physics/tgs_solve.h"

void phys_stage_integrate(const phys_integrate_args_t *args)
{
    if (!args) {
        return;
    }

    const phys_body_t *bodies_in       = args->bodies_in;
    const phys_velocity_t *velocities  = args->velocities;
    phys_body_t *bodies_out            = args->bodies_out;

    if (!bodies_in || !bodies_out || !velocities) {
        return;
    }

    const float dt                     = args->dt;
    const phys_vec3_t gravity          = args->gravity;
    const float sleep_lin_thresh       = args->sleep_threshold_linear;
    const float sleep_ang_thresh       = args->sleep_threshold_angular;
    const uint32_t sleep_delay         = args->sleep_delay_frames;
    const uint32_t cur_sub             = args->current_substep;
    const uint32_t *tier_subs          = args->tier_substep_counts;
    const float vel_damp               = args->velocity_damping;

    for (uint32_t i = 0; i < args->body_count; ++i) {
        const phys_body_t *in = &bodies_in[i];
        phys_body_t *out      = &bodies_out[i];

        /* Copy body from input to output. */
        *out = *in;

        /* Skip static and kinematic bodies. */
        if (phys_body_is_static(in) || phys_body_is_kinematic(in)) {
            continue;
        }

        /* Skip bodies whose tier doesn't need this substep. */
        if (tier_subs) {
            uint32_t ts = tier_subs[in->tier];
            if (ts == 0) { ts = 1; }
            if (cur_sub >= ts) { continue; }
        }

        /* Update velocity from solver output. */
        out->linear_vel  = velocities[i].linear;
        out->angular_vel = velocities[i].angular;

        /* Sanitize NaN/Inf velocities — clamp to zero to prevent
         * cascading corruption across ticks. */
        if (isnan(out->linear_vel.x) || isnan(out->linear_vel.y) ||
            isnan(out->linear_vel.z) || isinf(out->linear_vel.x) ||
            isinf(out->linear_vel.y) || isinf(out->linear_vel.z)) {
            out->linear_vel = (phys_vec3_t){0.0f, 0.0f, 0.0f};
        }
        if (isnan(out->angular_vel.x) || isnan(out->angular_vel.y) ||
            isnan(out->angular_vel.z) || isinf(out->angular_vel.x) ||
            isinf(out->angular_vel.y) || isinf(out->angular_vel.z)) {
            out->angular_vel = (phys_vec3_t){0.0f, 0.0f, 0.0f};
        }

        /* Apply gravity (only if body is not sleeping). */
        if (!phys_body_is_sleeping(in)) {
            out->linear_vel = vec3_add(out->linear_vel,
                                       vec3_scale(gravity, dt));
        }

        /* Apply velocity damping to dissipate energy over time. */
        if (vel_damp < 1.0f) {
            out->linear_vel  = vec3_scale(out->linear_vel, vel_damp);
            out->angular_vel = vec3_scale(out->angular_vel, vel_damp);
        }

        /* Clamp velocity magnitude to prevent runaway speeds.
         * 100 m/s linear, 50 rad/s angular are generous limits. */
        {
            float lin_speed = vec3_magnitude(out->linear_vel);
            if (lin_speed > 100.0f) {
                out->linear_vel = vec3_scale(out->linear_vel,
                                             100.0f / lin_speed);
            }
            float ang_speed = vec3_magnitude(out->angular_vel);
            if (ang_speed > 50.0f) {
                out->angular_vel = vec3_scale(out->angular_vel,
                                              50.0f / ang_speed);
            }
        }

        /* Integrate position: position += linear_vel * dt. */
        out->position = vec3_add(in->position,
                                 vec3_scale(out->linear_vel, dt));

        /* Sanitize NaN/Inf positions — revert to input to prevent
         * cascading corruption from position projection or solver. */
        if (isnan(out->position.x) || isnan(out->position.y) ||
            isnan(out->position.z) || isinf(out->position.x) ||
            isinf(out->position.y) || isinf(out->position.z)) {
            out->position = in->position;
            out->linear_vel = (phys_vec3_t){0.0f, 0.0f, 0.0f};
            out->angular_vel = (phys_vec3_t){0.0f, 0.0f, 0.0f};
        }

        /* Integrate orientation via quaternion derivative:
         *   omega_quat = {angular_vel.x, angular_vel.y, angular_vel.z, 0}
         *   dq = quat_mul(omega_quat, orientation)
         *   orientation += 0.5 * dq * dt  (component-wise)
         *   then normalize. */
        phys_quat_t omega_q = {
            out->angular_vel.x,
            out->angular_vel.y,
            out->angular_vel.z,
            0.0f
        };
        phys_quat_t dq = quat_mul(omega_q, in->orientation);
        float half_dt = 0.5f * dt;
        out->orientation.x = in->orientation.x + dq.x * half_dt;
        out->orientation.y = in->orientation.y + dq.y * half_dt;
        out->orientation.z = in->orientation.z + dq.z * half_dt;
        out->orientation.w = in->orientation.w + dq.w * half_dt;
        out->orientation = quat_normalize_safe(out->orientation, 1e-8f);

        /* Sleep detection. */
        float linear_speed  = vec3_magnitude(out->linear_vel);
        float angular_speed = vec3_magnitude(out->angular_vel);

        if (linear_speed < sleep_lin_thresh &&
            angular_speed < sleep_ang_thresh) {
            /* Body is below threshold: increment sleep counter. */
            if (out->sleep_counter < 255) {
                out->sleep_counter++;
            }
            if (out->sleep_counter >= sleep_delay) {
                out->flags |= PHYS_BODY_FLAG_SLEEPING;
            }
        } else {
            /* Body is active: reset sleep state. */
            out->sleep_counter = 0;
            out->flags &= ~PHYS_BODY_FLAG_SLEEPING;
        }
    }
}
