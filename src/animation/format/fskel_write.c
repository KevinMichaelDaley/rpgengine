/**
 * @file fskel_write.c
 * @brief .fskel JSON format writer.
 *
 * Writes skeleton hierarchy, constraints, IBMs, colliders, and joint
 * descriptors as a human-readable JSON file.
 *
 * Non-static functions: 1 (fskel_write)
 */

#include "ferrum/animation/fskel_loader.h"
#include "ferrum/animation/fskel_format.h"
#include "ferrum/animation/constraint_params.h"
#include "ferrum/animation/bone_collider.h"
#include "ferrum/animation/bone_joint_desc.h"
#include "ferrum/math/mat4.h"

#include <stdio.h>
#include <string.h>

/* ── Helpers ────────────────────────────────────────────────────── */

/** @brief Write a mat4_t as a JSON array of 16 floats. */
static void write_mat4_(FILE *f, const mat4_t *m) {
    fprintf(f, "[");
    for (int i = 0; i < 16; i++) {
        fprintf(f, "%s%.8g", i ? "," : "", (double)m->m[i]);
    }
    fprintf(f, "]");
}

/** @brief Map constraint_type_t to string name for JSON output. */
static const char *constraint_type_str_(constraint_type_t t) {
    static const char *names[] = {
        "IK", "SPLINE_IK", "CHILD_OF", "COPY_TRANSFORMS",
        "COPY_ROTATION", "COPY_LOCATION", "COPY_SCALE",
        "DAMPED_TRACK", "TRACK_TO", "LOCKED_TRACK",
        "LIMIT_ROTATION", "LIMIT_LOCATION", "LIMIT_SCALE",
        "TRANSFORMATION", "ACTION", "CLAMP_TO", "FLOOR",
        "MAINTAIN_VOLUME", "SHRINKWRAP", "PIVOT"
    };
    if ((int)t >= 0 && (int)t < (int)(sizeof(names)/sizeof(names[0])))
        return names[t];
    return "UNKNOWN";
}

/** @brief Map bone_collider_shape_t to string name. */
static const char *collider_shape_str_(uint32_t s) {
    switch (s) {
    case BONE_COLLIDER_CAPSULE:     return "capsule";
    case BONE_COLLIDER_BOX:         return "box";
    case BONE_COLLIDER_SPHERE:      return "sphere";
    case BONE_COLLIDER_CONVEX_HULL: return "convex_hull";
    default:                        return "none";
    }
}

/**
 * @brief Write constraint params as JSON object fields.
 *
 * Writes the type-specific parameters for a constraint_def_t.
 */
static void write_constraint_params_(FILE *f, const constraint_def_t *c) {
    fprintf(f, "{");
    switch (c->type) {
    case CONSTRAINT_IK:
        fprintf(f, "\"chain_length\":%u,\"pole_target\":%d,"
                   "\"iterations\":%u,\"weight\":%.8g,"
                   "\"orient_weight\":%.8g,\"use_tail\":%s",
                c->params.ik.chain_length,
                (c->params.ik.pole_target_idx == UINT32_MAX) ? -1 : (int)c->params.ik.pole_target_idx,
                c->params.ik.iterations,
                (double)c->params.ik.weight,
                (double)c->params.ik.orient_weight,
                c->params.ik.use_tail ? "true" : "false");
        break;

    case CONSTRAINT_SPLINE_IK:
        fprintf(f, "\"chain_length\":%u,\"twist_axis\":%u,\"control_points\":[",
                c->params.spline_ik.chain_length,
                (unsigned)c->params.spline_ik.twist_axis);
        for (uint32_t i = 0; i < c->params.spline_ik.control_point_count; i++) {
            fprintf(f, "%s[%.8g,%.8g,%.8g]", i ? "," : "",
                    (double)c->params.spline_ik.control_points[i*3],
                    (double)c->params.spline_ik.control_points[i*3+1],
                    (double)c->params.spline_ik.control_points[i*3+2]);
        }
        fprintf(f, "]");
        break;

    case CONSTRAINT_CHILD_OF:
        fprintf(f, "\"use_location_x\":%s,\"use_location_y\":%s,\"use_location_z\":%s,"
                   "\"use_rotation_x\":%s,\"use_rotation_y\":%s,\"use_rotation_z\":%s,"
                   "\"use_scale_x\":%s,\"use_scale_y\":%s,\"use_scale_z\":%s,"
                   "\"inverse_matrix\":",
                c->params.child_of.use_location_x ? "true" : "false",
                c->params.child_of.use_location_y ? "true" : "false",
                c->params.child_of.use_location_z ? "true" : "false",
                c->params.child_of.use_rotation_x ? "true" : "false",
                c->params.child_of.use_rotation_y ? "true" : "false",
                c->params.child_of.use_rotation_z ? "true" : "false",
                c->params.child_of.use_scale_x ? "true" : "false",
                c->params.child_of.use_scale_y ? "true" : "false",
                c->params.child_of.use_scale_z ? "true" : "false");
        write_mat4_(f, &c->params.child_of.inverse_matrix);
        break;

    case CONSTRAINT_COPY_TRANSFORMS:
        fprintf(f, "\"mix_mode\":%u",
                (unsigned)c->params.copy_transforms.mix_mode);
        break;

    case CONSTRAINT_COPY_ROTATION:
        fprintf(f, "\"mix_mode\":%u,"
                   "\"use_x\":%s,\"use_y\":%s,\"use_z\":%s,"
                   "\"invert_x\":%s,\"invert_y\":%s,\"invert_z\":%s",
                (unsigned)c->params.copy_rotation.mix_mode,
                c->params.copy_rotation.use_x ? "true" : "false",
                c->params.copy_rotation.use_y ? "true" : "false",
                c->params.copy_rotation.use_z ? "true" : "false",
                c->params.copy_rotation.invert_x ? "true" : "false",
                c->params.copy_rotation.invert_y ? "true" : "false",
                c->params.copy_rotation.invert_z ? "true" : "false");
        break;

    case CONSTRAINT_COPY_LOCATION:
        fprintf(f, "\"use_x\":%s,\"use_y\":%s,\"use_z\":%s,"
                   "\"invert_x\":%s,\"invert_y\":%s,\"invert_z\":%s,"
                   "\"offset\":%s",
                c->params.copy_location.use_x ? "true" : "false",
                c->params.copy_location.use_y ? "true" : "false",
                c->params.copy_location.use_z ? "true" : "false",
                c->params.copy_location.invert_x ? "true" : "false",
                c->params.copy_location.invert_y ? "true" : "false",
                c->params.copy_location.invert_z ? "true" : "false",
                c->params.copy_location.offset ? "true" : "false");
        break;

    case CONSTRAINT_COPY_SCALE:
        fprintf(f, "\"use_x\":%s,\"use_y\":%s,\"use_z\":%s,"
                   "\"power\":%.8g,\"offset\":%s",
                c->params.copy_scale.use_x ? "true" : "false",
                c->params.copy_scale.use_y ? "true" : "false",
                c->params.copy_scale.use_z ? "true" : "false",
                (double)c->params.copy_scale.power,
                c->params.copy_scale.offset ? "true" : "false");
        break;

    case CONSTRAINT_DAMPED_TRACK:
        fprintf(f, "\"track_axis\":%u",
                (unsigned)c->params.damped_track.track_axis);
        break;

    case CONSTRAINT_TRACK_TO:
        fprintf(f, "\"track_axis\":%u,\"up_axis\":%u",
                (unsigned)c->params.track_to.track_axis,
                (unsigned)c->params.track_to.up_axis);
        break;

    case CONSTRAINT_LOCKED_TRACK:
        fprintf(f, "\"track_axis\":%u,\"lock_axis\":%u",
                (unsigned)c->params.locked_track.track_axis,
                (unsigned)c->params.locked_track.lock_axis);
        break;

    case CONSTRAINT_LIMIT_ROTATION:
        fprintf(f, "\"min_x\":%.8g,\"max_x\":%.8g,"
                   "\"min_y\":%.8g,\"max_y\":%.8g,"
                   "\"min_z\":%.8g,\"max_z\":%.8g,"
                   "\"use_limit_x\":%s,\"use_limit_y\":%s,\"use_limit_z\":%s",
                (double)c->params.limit_rotation.min_x,
                (double)c->params.limit_rotation.max_x,
                (double)c->params.limit_rotation.min_y,
                (double)c->params.limit_rotation.max_y,
                (double)c->params.limit_rotation.min_z,
                (double)c->params.limit_rotation.max_z,
                c->params.limit_rotation.use_limit_x ? "true" : "false",
                c->params.limit_rotation.use_limit_y ? "true" : "false",
                c->params.limit_rotation.use_limit_z ? "true" : "false");
        break;

    case CONSTRAINT_LIMIT_LOCATION:
        fprintf(f, "\"min_x\":%.8g,\"max_x\":%.8g,"
                   "\"min_y\":%.8g,\"max_y\":%.8g,"
                   "\"min_z\":%.8g,\"max_z\":%.8g,"
                   "\"use_min_x\":%s,\"use_max_x\":%s,"
                   "\"use_min_y\":%s,\"use_max_y\":%s,"
                   "\"use_min_z\":%s,\"use_max_z\":%s",
                (double)c->params.limit_location.min_x,
                (double)c->params.limit_location.max_x,
                (double)c->params.limit_location.min_y,
                (double)c->params.limit_location.max_y,
                (double)c->params.limit_location.min_z,
                (double)c->params.limit_location.max_z,
                c->params.limit_location.use_min_x ? "true" : "false",
                c->params.limit_location.use_max_x ? "true" : "false",
                c->params.limit_location.use_min_y ? "true" : "false",
                c->params.limit_location.use_max_y ? "true" : "false",
                c->params.limit_location.use_min_z ? "true" : "false",
                c->params.limit_location.use_max_z ? "true" : "false");
        break;

    case CONSTRAINT_LIMIT_SCALE:
        fprintf(f, "\"min_x\":%.8g,\"max_x\":%.8g,"
                   "\"min_y\":%.8g,\"max_y\":%.8g,"
                   "\"min_z\":%.8g,\"max_z\":%.8g,"
                   "\"use_min_x\":%s,\"use_max_x\":%s,"
                   "\"use_min_y\":%s,\"use_max_y\":%s,"
                   "\"use_min_z\":%s,\"use_max_z\":%s",
                (double)c->params.limit_scale.min_x,
                (double)c->params.limit_scale.max_x,
                (double)c->params.limit_scale.min_y,
                (double)c->params.limit_scale.max_y,
                (double)c->params.limit_scale.min_z,
                (double)c->params.limit_scale.max_z,
                c->params.limit_scale.use_min_x ? "true" : "false",
                c->params.limit_scale.use_max_x ? "true" : "false",
                c->params.limit_scale.use_min_y ? "true" : "false",
                c->params.limit_scale.use_max_y ? "true" : "false",
                c->params.limit_scale.use_min_z ? "true" : "false",
                c->params.limit_scale.use_max_z ? "true" : "false");
        break;

    case CONSTRAINT_TRANSFORMATION:
        fprintf(f, "\"from_channel\":%u,\"to_channel\":%u,"
                   "\"from_min\":%.8g,\"from_max\":%.8g,"
                   "\"to_min\":%.8g,\"to_max\":%.8g,"
                   "\"extrapolate\":%s",
                (unsigned)c->params.transformation.from_channel,
                (unsigned)c->params.transformation.to_channel,
                (double)c->params.transformation.from_min,
                (double)c->params.transformation.from_max,
                (double)c->params.transformation.to_min,
                (double)c->params.transformation.to_max,
                c->params.transformation.extrapolate ? "true" : "false");
        break;

    case CONSTRAINT_ACTION:
        fprintf(f, "\"action_clip_idx\":%u,\"transform_channel\":%u,"
                   "\"min_value\":%.8g,\"max_value\":%.8g",
                c->params.action.action_clip_idx,
                (unsigned)c->params.action.transform_channel,
                (double)c->params.action.min_value,
                (double)c->params.action.max_value);
        break;

    case CONSTRAINT_CLAMP_TO:
        fprintf(f, "\"main_axis\":%u,\"cyclic\":%s,\"control_points\":[",
                (unsigned)c->params.clamp_to.main_axis,
                c->params.clamp_to.cyclic ? "true" : "false");
        for (uint32_t i = 0; i < c->params.clamp_to.control_point_count; i++) {
            fprintf(f, "%s[%.8g,%.8g,%.8g]", i ? "," : "",
                    (double)c->params.clamp_to.control_points[i*3],
                    (double)c->params.clamp_to.control_points[i*3+1],
                    (double)c->params.clamp_to.control_points[i*3+2]);
        }
        fprintf(f, "]");
        break;

    case CONSTRAINT_FLOOR:
        fprintf(f, "\"offset\":%.8g,\"use_rotation\":%s,\"floor_location\":%u",
                (double)c->params.floor.offset,
                c->params.floor.use_rotation ? "true" : "false",
                (unsigned)c->params.floor.floor_location);
        break;

    case CONSTRAINT_MAINTAIN_VOLUME:
        fprintf(f, "\"free_axis\":%u,\"volume\":%.8g",
                (unsigned)c->params.maintain_volume.free_axis,
                (double)c->params.maintain_volume.volume);
        break;

    case CONSTRAINT_SHRINKWRAP:
        fprintf(f, "\"shrinkwrap_type\":%u,\"distance\":%.8g",
                (unsigned)c->params.shrinkwrap.shrinkwrap_type,
                (double)c->params.shrinkwrap.distance);
        break;

    case CONSTRAINT_PIVOT:
        fprintf(f, "\"offset_x\":%.8g,\"offset_y\":%.8g,\"offset_z\":%.8g,"
                   "\"rotation_range\":%.8g",
                (double)c->params.pivot.offset[0],
                (double)c->params.pivot.offset[1],
                (double)c->params.pivot.offset[2],
                (double)c->params.pivot.rotation_range);
        break;

    default:
        break;
    }
    fprintf(f, "}");
}

/* ── Public API ─────────────────────────────────────────────────── */

bool fskel_write(const char *path,
                 const skeleton_def_t *skel,
                 const mat4_t *ibms,
                 uint32_t ibm_count) {
    if (!path || !skel) return false;

    FILE *f = fopen(path, "w");
    if (!f) return false;

    uint32_t n = skel->joint_count;

    fprintf(f, "{\n \"version\": %u,\n \"joints\": [\n", FSKEL_VERSION);

    for (uint32_t i = 0; i < n; i++) {
        fprintf(f, "%s {\n", i ? ",\n" : "");

        /* Name. */
        fprintf(f, "  \"name\": \"%s\",\n", skel->joint_names[i]);

        /* Parent. */
        int32_t parent = (skel->parent_indices[i] == UINT32_MAX)
                         ? -1 : (int32_t)skel->parent_indices[i];
        fprintf(f, "  \"parent\": %d,\n", parent);

        /* Rest transforms. */
        fprintf(f, "  \"rest_local\": ");
        write_mat4_(f, &skel->rest_local[i]);
        fprintf(f, ",\n  \"rest_world\": ");
        write_mat4_(f, &skel->rest_world[i]);
        fprintf(f, ",\n");

        /* Constraints. */
        uint32_t nc = skel->constraint_counts ? skel->constraint_counts[i] : 0;
        fprintf(f, "  \"constraints\": [");
        for (uint32_t ci = 0; ci < nc; ci++) {
            const constraint_def_t *c =
                &skel->constraints[i * skel->max_constraints_per_joint + ci];
            fprintf(f, "%s\n   {\"type\":\"%s\",\"influence\":%.8g,"
                       "\"owner_space\":%u,\"target_space\":%u,"
                       "\"target_bone\":%d,\"params\":",
                    ci ? "," : "",
                    constraint_type_str_(c->type),
                    (double)c->influence,
                    (unsigned)c->owner_space,
                    (unsigned)c->target_space,
                    (c->target_bone_idx == UINT32_MAX) ? -1 : (int)c->target_bone_idx);
            write_constraint_params_(f, c);
            fprintf(f, "}");
        }
        fprintf(f, "],\n");

        /* Collider. */
        fprintf(f, "  \"collider\": {");
        if (skel->colliders) {
            const bone_collider_desc_t *cd = &skel->colliders[i];
            fprintf(f, "\"shape\":\"%s\",\"params\":[%.8g,%.8g,%.8g,%.8g,%.8g,%.8g],"
                       "\"ccd\":%s,\"kinematic\":%s,\"mass\":%.8g,"
                       "\"hull_offset\":%u,\"hull_count\":%u,\"collision_group\":%u",
                    collider_shape_str_(cd->shape_type),
                    (double)cd->params[0], (double)cd->params[1],
                    (double)cd->params[2], (double)cd->params[3],
                    (double)cd->params[4], (double)cd->params[5],
                    cd->ccd_enabled ? "true" : "false",
                    cd->is_kinematic ? "true" : "false",
                    (double)cd->mass,
                    cd->hull_offset, cd->hull_count, cd->collision_group);
        } else {
            fprintf(f, "\"shape\":\"none\",\"params\":[0,0,0,0,0,0],"
                       "\"ccd\":false,\"kinematic\":false,\"mass\":0,"
                       "\"hull_offset\":0,\"hull_count\":0,\"collision_group\":0");
        }
        fprintf(f, "},\n");

        /* Joint descriptor. */
        fprintf(f, "  \"joint_desc\": {");
        if (skel->joints) {
            const bone_joint_desc_t *jd = &skel->joints[i];
            fprintf(f, "\"type\":%u,\"axis\":[%.8g,%.8g,%.8g],"
                       "\"rest_length\":%.8g,"
                       "\"limit_min\":[%.8g,%.8g,%.8g],"
                       "\"limit_max\":[%.8g,%.8g,%.8g],"
                       "\"limit_axes\":%u",
                    jd->joint_type,
                    (double)jd->axis[0], (double)jd->axis[1], (double)jd->axis[2],
                    (double)jd->rest_length,
                    (double)jd->limit_min[0], (double)jd->limit_min[1], (double)jd->limit_min[2],
                    (double)jd->limit_max[0], (double)jd->limit_max[1], (double)jd->limit_max[2],
                    jd->limit_axes);
        } else {
            fprintf(f, "\"type\":0,\"axis\":[0,1,0],\"rest_length\":0,"
                       "\"limit_min\":[0,0,0],\"limit_max\":[0,0,0],\"limit_axes\":0");
        }
        fprintf(f, "}\n }");
    }

    fprintf(f, "\n ],\n");

    /* IBMs. */
    fprintf(f, " \"ibms\": [\n");
    for (uint32_t i = 0; i < ibm_count; i++) {
        fprintf(f, "  ");
        write_mat4_(f, &ibms[i]);
        fprintf(f, "%s\n", (i + 1 < ibm_count) ? "," : "");
    }
    fprintf(f, " ],\n");

    /* Hull vertices. */
    fprintf(f, " \"hull_vertices\": [");
    if (skel->hull_vertices && skel->hull_vertex_count > 0) {
        for (uint32_t i = 0; i < skel->hull_vertex_count * 3; i++) {
            fprintf(f, "%s%.8g", i ? "," : "",
                    (double)skel->hull_vertices[i]);
        }
    }
    fprintf(f, "]\n}\n");

    fclose(f);
    return true;
}
