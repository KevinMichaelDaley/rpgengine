/**
 * @file fskel_load.c
 * @brief .fskel JSON format loader.
 *
 * Reads skeleton hierarchy, constraints, IBMs, colliders, and joint
 * descriptors from a JSON file.  Uses the arena-based json_parse.h
 * parser.
 *
 * Non-static functions: 1 (fskel_load)
 */

#include "ferrum/animation/fskel_loader.h"
#include "ferrum/animation/fskel_format.h"
#include "ferrum/animation/constraint_params.h"
#include "ferrum/animation/bone_collider.h"
#include "ferrum/animation/bone_joint_desc.h"
#include "ferrum/editor/json_parse.h"
#include "ferrum/math/mat4.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief Maximum joints to prevent insane allocations. */
#define FSKEL_MAX_JOINTS 4096

/* ── Helpers ────────────────────────────────────────────────────── */

/** @brief Read a float from a JSON number value, returning def on failure. */
static float jfloat_(const json_value_t *v, float def) {
    return (v && v->type == JSON_NUMBER) ? (float)v->number : def;
}

/** @brief Read an int from a JSON number value, returning def on failure. */
static int32_t jint_(const json_value_t *v, int32_t def) {
    return (v && v->type == JSON_NUMBER) ? (int32_t)v->number : def;
}

/** @brief Read a uint32 from a JSON number, treating -1 as UINT32_MAX. */
static uint32_t juint_(const json_value_t *v, uint32_t def) {
    if (!v || v->type != JSON_NUMBER) return def;
    int32_t i = (int32_t)v->number;
    return (i < 0) ? UINT32_MAX : (uint32_t)i;
}

/** @brief Read a bool from JSON, returning def on failure. */
static bool jbool_(const json_value_t *v, bool def) {
    if (!v) return def;
    if (v->type == JSON_BOOL) return v->boolean;
    if (v->type == JSON_NUMBER) return v->number != 0.0;
    return def;
}

/** @brief Read 16 floats from a JSON array into a mat4_t. */
static bool jmat4_(const json_value_t *arr, mat4_t *out) {
    if (!arr || arr->type != JSON_ARRAY || arr->array.count < 16) return false;
    for (uint32_t i = 0; i < 16; i++) {
        out->m[i] = jfloat_(&arr->array.items[i], 0.0f);
    }
    return true;
}

/**
 * @brief Match a JSON string value against a null-terminated C string.
 * @return true if they match.
 */
static bool jstreq_(const json_value_t *v, const char *s) {
    if (!v || v->type != JSON_STRING || !s) return false;
    size_t slen = strlen(s);
    return v->string.len == (uint32_t)slen &&
           memcmp(v->string.ptr, s, slen) == 0;
}

/**
 * @brief Map a constraint type string to constraint_type_t.
 * @return The type, or -1 if unrecognized.
 */
static int constraint_type_from_string_(const json_value_t *v) {
    if (!v || v->type != JSON_STRING) return -1;
    static const struct { const char *name; int type; } map[] = {
        {"IK",                CONSTRAINT_IK},
        {"SPLINE_IK",         CONSTRAINT_SPLINE_IK},
        {"CHILD_OF",          CONSTRAINT_CHILD_OF},
        {"COPY_TRANSFORMS",   CONSTRAINT_COPY_TRANSFORMS},
        {"COPY_ROTATION",     CONSTRAINT_COPY_ROTATION},
        {"COPY_LOCATION",     CONSTRAINT_COPY_LOCATION},
        {"COPY_SCALE",        CONSTRAINT_COPY_SCALE},
        {"DAMPED_TRACK",      CONSTRAINT_DAMPED_TRACK},
        {"TRACK_TO",          CONSTRAINT_TRACK_TO},
        {"LOCKED_TRACK",      CONSTRAINT_LOCKED_TRACK},
        {"LIMIT_ROTATION",    CONSTRAINT_LIMIT_ROTATION},
        {"LIMIT_LOCATION",    CONSTRAINT_LIMIT_LOCATION},
        {"LIMIT_SCALE",       CONSTRAINT_LIMIT_SCALE},
        {"TRANSFORMATION",    CONSTRAINT_TRANSFORMATION},
        {"ACTION",            CONSTRAINT_ACTION},
        {"CLAMP_TO",          CONSTRAINT_CLAMP_TO},
        {"FLOOR",             CONSTRAINT_FLOOR},
        {"MAINTAIN_VOLUME",   CONSTRAINT_MAINTAIN_VOLUME},
        {"SHRINKWRAP",        CONSTRAINT_SHRINKWRAP},
        {"PIVOT",             CONSTRAINT_PIVOT},
    };
    for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
        if (jstreq_(v, map[i].name)) return map[i].type;
    }
    return -1;
}

/**
 * @brief Map a collider shape string to bone_collider_shape_t.
 */
static uint32_t collider_shape_from_string_(const json_value_t *v) {
    if (jstreq_(v, "capsule"))     return BONE_COLLIDER_CAPSULE;
    if (jstreq_(v, "box"))         return BONE_COLLIDER_BOX;
    if (jstreq_(v, "sphere"))      return BONE_COLLIDER_SPHERE;
    if (jstreq_(v, "convex_hull")) return BONE_COLLIDER_CONVEX_HULL;
    if (jstreq_(v, "point"))       return BONE_COLLIDER_POINT;
    return BONE_COLLIDER_NONE;
}

/* ── Constraint param parsing ───────────────────────────────────── */

/**
 * @brief Parse constraint params from a JSON object into constraint_def_t.
 *
 * Fills in the params union based on the constraint type.
 */
static void parse_constraint_params_(constraint_def_t *def,
                                     const json_value_t *params) {
    if (!params || params->type != JSON_OBJECT) return;

    switch (def->type) {
    case CONSTRAINT_IK:
        def->params.ik.chain_length    = juint_(json_object_get(params, "chain_length"), 0);
        def->params.ik.pole_target_idx = juint_(json_object_get(params, "pole_target"), UINT32_MAX);
        def->params.ik.iterations      = juint_(json_object_get(params, "iterations"), 10);
        def->params.ik.weight          = jfloat_(json_object_get(params, "weight"), 1.0f);
        def->params.ik.orient_weight   = jfloat_(json_object_get(params, "orient_weight"), 0.0f);
        def->params.ik.use_tail        = jbool_(json_object_get(params, "use_tail"), true);
        break;

    case CONSTRAINT_SPLINE_IK: {
        def->params.spline_ik.chain_length = juint_(json_object_get(params, "chain_length"), 0);
        const json_value_t *cps = json_object_get(params, "control_points");
        def->params.spline_ik.control_point_count = 0;
        if (cps && cps->type == JSON_ARRAY) {
            uint32_t n = cps->array.count < 16 ? cps->array.count : 16;
            for (uint32_t i = 0; i < n; i++) {
                const json_value_t *pt = &cps->array.items[i];
                if (pt->type == JSON_ARRAY && pt->array.count >= 3) {
                    def->params.spline_ik.control_points[i * 3 + 0] =
                        jfloat_(&pt->array.items[0], 0.0f);
                    def->params.spline_ik.control_points[i * 3 + 1] =
                        jfloat_(&pt->array.items[1], 0.0f);
                    def->params.spline_ik.control_points[i * 3 + 2] =
                        jfloat_(&pt->array.items[2], 0.0f);
                }
            }
            def->params.spline_ik.control_point_count = n;
        }
        def->params.spline_ik.twist_axis = juint_(json_object_get(params, "twist_axis"), 0);
        break;
    }

    case CONSTRAINT_CHILD_OF:
        def->params.child_of.use_location_x = jbool_(json_object_get(params, "use_location_x"), true);
        def->params.child_of.use_location_y = jbool_(json_object_get(params, "use_location_y"), true);
        def->params.child_of.use_location_z = jbool_(json_object_get(params, "use_location_z"), true);
        def->params.child_of.use_rotation_x = jbool_(json_object_get(params, "use_rotation_x"), true);
        def->params.child_of.use_rotation_y = jbool_(json_object_get(params, "use_rotation_y"), true);
        def->params.child_of.use_rotation_z = jbool_(json_object_get(params, "use_rotation_z"), true);
        def->params.child_of.use_scale_x    = jbool_(json_object_get(params, "use_scale_x"), true);
        def->params.child_of.use_scale_y    = jbool_(json_object_get(params, "use_scale_y"), true);
        def->params.child_of.use_scale_z    = jbool_(json_object_get(params, "use_scale_z"), true);
        jmat4_(json_object_get(params, "inverse_matrix"),
               &def->params.child_of.inverse_matrix);
        break;

    case CONSTRAINT_COPY_TRANSFORMS:
        def->params.copy_transforms.mix_mode = juint_(json_object_get(params, "mix_mode"), 0);
        break;

    case CONSTRAINT_COPY_ROTATION:
        def->params.copy_rotation.mix_mode = juint_(json_object_get(params, "mix_mode"), 0);
        def->params.copy_rotation.use_x    = jbool_(json_object_get(params, "use_x"), true);
        def->params.copy_rotation.use_y    = jbool_(json_object_get(params, "use_y"), true);
        def->params.copy_rotation.use_z    = jbool_(json_object_get(params, "use_z"), true);
        def->params.copy_rotation.invert_x = jbool_(json_object_get(params, "invert_x"), false);
        def->params.copy_rotation.invert_y = jbool_(json_object_get(params, "invert_y"), false);
        def->params.copy_rotation.invert_z = jbool_(json_object_get(params, "invert_z"), false);
        break;

    case CONSTRAINT_COPY_LOCATION:
        def->params.copy_location.use_x    = jbool_(json_object_get(params, "use_x"), true);
        def->params.copy_location.use_y    = jbool_(json_object_get(params, "use_y"), true);
        def->params.copy_location.use_z    = jbool_(json_object_get(params, "use_z"), true);
        def->params.copy_location.invert_x = jbool_(json_object_get(params, "invert_x"), false);
        def->params.copy_location.invert_y = jbool_(json_object_get(params, "invert_y"), false);
        def->params.copy_location.invert_z = jbool_(json_object_get(params, "invert_z"), false);
        def->params.copy_location.offset   = jbool_(json_object_get(params, "offset"), false);
        break;

    case CONSTRAINT_COPY_SCALE:
        def->params.copy_scale.use_x  = jbool_(json_object_get(params, "use_x"), true);
        def->params.copy_scale.use_y  = jbool_(json_object_get(params, "use_y"), true);
        def->params.copy_scale.use_z  = jbool_(json_object_get(params, "use_z"), true);
        def->params.copy_scale.power  = jfloat_(json_object_get(params, "power"), 1.0f);
        def->params.copy_scale.offset = jbool_(json_object_get(params, "offset"), false);
        break;

    case CONSTRAINT_DAMPED_TRACK:
        def->params.damped_track.track_axis = juint_(json_object_get(params, "track_axis"), 1);
        break;

    case CONSTRAINT_TRACK_TO:
        def->params.track_to.track_axis = juint_(json_object_get(params, "track_axis"), 1);
        def->params.track_to.up_axis    = juint_(json_object_get(params, "up_axis"), 2);
        break;

    case CONSTRAINT_LOCKED_TRACK:
        def->params.locked_track.track_axis = juint_(json_object_get(params, "track_axis"), 1);
        def->params.locked_track.lock_axis  = juint_(json_object_get(params, "lock_axis"), 2);
        break;

    case CONSTRAINT_LIMIT_ROTATION:
        def->params.limit_rotation.min_x       = jfloat_(json_object_get(params, "min_x"), 0.0f);
        def->params.limit_rotation.max_x       = jfloat_(json_object_get(params, "max_x"), 0.0f);
        def->params.limit_rotation.min_y       = jfloat_(json_object_get(params, "min_y"), 0.0f);
        def->params.limit_rotation.max_y       = jfloat_(json_object_get(params, "max_y"), 0.0f);
        def->params.limit_rotation.min_z       = jfloat_(json_object_get(params, "min_z"), 0.0f);
        def->params.limit_rotation.max_z       = jfloat_(json_object_get(params, "max_z"), 0.0f);
        def->params.limit_rotation.use_limit_x = jbool_(json_object_get(params, "use_limit_x"), false);
        def->params.limit_rotation.use_limit_y = jbool_(json_object_get(params, "use_limit_y"), false);
        def->params.limit_rotation.use_limit_z = jbool_(json_object_get(params, "use_limit_z"), false);
        break;

    case CONSTRAINT_LIMIT_LOCATION:
        def->params.limit_location.min_x     = jfloat_(json_object_get(params, "min_x"), 0.0f);
        def->params.limit_location.max_x     = jfloat_(json_object_get(params, "max_x"), 0.0f);
        def->params.limit_location.min_y     = jfloat_(json_object_get(params, "min_y"), 0.0f);
        def->params.limit_location.max_y     = jfloat_(json_object_get(params, "max_y"), 0.0f);
        def->params.limit_location.min_z     = jfloat_(json_object_get(params, "min_z"), 0.0f);
        def->params.limit_location.max_z     = jfloat_(json_object_get(params, "max_z"), 0.0f);
        def->params.limit_location.use_min_x = jbool_(json_object_get(params, "use_min_x"), false);
        def->params.limit_location.use_max_x = jbool_(json_object_get(params, "use_max_x"), false);
        def->params.limit_location.use_min_y = jbool_(json_object_get(params, "use_min_y"), false);
        def->params.limit_location.use_max_y = jbool_(json_object_get(params, "use_max_y"), false);
        def->params.limit_location.use_min_z = jbool_(json_object_get(params, "use_min_z"), false);
        def->params.limit_location.use_max_z = jbool_(json_object_get(params, "use_max_z"), false);
        break;

    case CONSTRAINT_LIMIT_SCALE:
        def->params.limit_scale.min_x     = jfloat_(json_object_get(params, "min_x"), 0.0f);
        def->params.limit_scale.max_x     = jfloat_(json_object_get(params, "max_x"), 0.0f);
        def->params.limit_scale.min_y     = jfloat_(json_object_get(params, "min_y"), 0.0f);
        def->params.limit_scale.max_y     = jfloat_(json_object_get(params, "max_y"), 0.0f);
        def->params.limit_scale.min_z     = jfloat_(json_object_get(params, "min_z"), 0.0f);
        def->params.limit_scale.max_z     = jfloat_(json_object_get(params, "max_z"), 0.0f);
        def->params.limit_scale.use_min_x = jbool_(json_object_get(params, "use_min_x"), false);
        def->params.limit_scale.use_max_x = jbool_(json_object_get(params, "use_max_x"), false);
        def->params.limit_scale.use_min_y = jbool_(json_object_get(params, "use_min_y"), false);
        def->params.limit_scale.use_max_y = jbool_(json_object_get(params, "use_max_y"), false);
        def->params.limit_scale.use_min_z = jbool_(json_object_get(params, "use_min_z"), false);
        def->params.limit_scale.use_max_z = jbool_(json_object_get(params, "use_max_z"), false);
        break;

    case CONSTRAINT_TRANSFORMATION:
        def->params.transformation.from_channel = juint_(json_object_get(params, "from_channel"), 0);
        def->params.transformation.to_channel   = juint_(json_object_get(params, "to_channel"), 0);
        def->params.transformation.from_min     = jfloat_(json_object_get(params, "from_min"), 0.0f);
        def->params.transformation.from_max     = jfloat_(json_object_get(params, "from_max"), 1.0f);
        def->params.transformation.to_min       = jfloat_(json_object_get(params, "to_min"), 0.0f);
        def->params.transformation.to_max       = jfloat_(json_object_get(params, "to_max"), 1.0f);
        def->params.transformation.extrapolate  = jbool_(json_object_get(params, "extrapolate"), false);
        break;

    case CONSTRAINT_ACTION:
        def->params.action.action_clip_idx    = juint_(json_object_get(params, "action_clip_idx"), 0);
        def->params.action.transform_channel  = juint_(json_object_get(params, "transform_channel"), 0);
        def->params.action.min_value          = jfloat_(json_object_get(params, "min_value"), 0.0f);
        def->params.action.max_value          = jfloat_(json_object_get(params, "max_value"), 1.0f);
        break;

    case CONSTRAINT_CLAMP_TO: {
        def->params.clamp_to.main_axis = juint_(json_object_get(params, "main_axis"), 0);
        const json_value_t *cps = json_object_get(params, "control_points");
        def->params.clamp_to.control_point_count = 0;
        if (cps && cps->type == JSON_ARRAY) {
            uint32_t n = cps->array.count < 16 ? cps->array.count : 16;
            for (uint32_t i = 0; i < n; i++) {
                const json_value_t *pt = &cps->array.items[i];
                if (pt->type == JSON_ARRAY && pt->array.count >= 3) {
                    def->params.clamp_to.control_points[i * 3 + 0] =
                        jfloat_(&pt->array.items[0], 0.0f);
                    def->params.clamp_to.control_points[i * 3 + 1] =
                        jfloat_(&pt->array.items[1], 0.0f);
                    def->params.clamp_to.control_points[i * 3 + 2] =
                        jfloat_(&pt->array.items[2], 0.0f);
                }
            }
            def->params.clamp_to.control_point_count = n;
        }
        def->params.clamp_to.cyclic = jbool_(json_object_get(params, "cyclic"), false);
        break;
    }

    case CONSTRAINT_FLOOR:
        def->params.floor.offset         = jfloat_(json_object_get(params, "offset"), 0.0f);
        def->params.floor.use_rotation   = jbool_(json_object_get(params, "use_rotation"), false);
        def->params.floor.floor_location = juint_(json_object_get(params, "floor_location"), 0);
        break;

    case CONSTRAINT_MAINTAIN_VOLUME:
        def->params.maintain_volume.free_axis = juint_(json_object_get(params, "free_axis"), 1);
        def->params.maintain_volume.volume    = jfloat_(json_object_get(params, "volume"), 1.0f);
        break;

    case CONSTRAINT_SHRINKWRAP:
        def->params.shrinkwrap.shrinkwrap_type = juint_(json_object_get(params, "shrinkwrap_type"), 0);
        def->params.shrinkwrap.distance        = jfloat_(json_object_get(params, "distance"), 0.0f);
        break;

    case CONSTRAINT_PIVOT:
        def->params.pivot.offset[0]      = jfloat_(json_object_get(params, "offset_x"), 0.0f);
        def->params.pivot.offset[1]      = jfloat_(json_object_get(params, "offset_y"), 0.0f);
        def->params.pivot.offset[2]      = jfloat_(json_object_get(params, "offset_z"), 0.0f);
        def->params.pivot.rotation_range = jfloat_(json_object_get(params, "rotation_range"), 0.0f);
        break;

    default:
        break;
    }
}

/* ── Public API ─────────────────────────────────────────────────── */

bool fskel_load(const char *path,
                skeleton_def_t *out_skel,
                mat4_t **out_ibms,
                uint32_t *out_ibm_count) {
    if (!path || !out_skel) return false;

    /* Read entire file into memory. */
    FILE *f = fopen(path, "rb");
    if (!f) return false;

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (file_size <= 0) { fclose(f); return false; }

    char *file_buf = (char *)malloc((size_t)file_size);
    if (!file_buf) { fclose(f); return false; }
    if (fread(file_buf, 1, (size_t)file_size, f) != (size_t)file_size) {
        free(file_buf);
        fclose(f);
        return false;
    }
    fclose(f);

    /* Parse JSON.  Arena size: ~32 bytes per JSON node; a 333-joint
     * skeleton with constraints, colliders, IBMs ≈ 200k nodes. */
    size_t arena_size = (size_t)file_size * 4;
    if (arena_size < 1024 * 1024) arena_size = 1024 * 1024;
    uint8_t *arena_buf = (uint8_t *)malloc(arena_size);
    if (!arena_buf) { free(file_buf); return false; }

    json_arena_t arena;
    json_arena_init(&arena, arena_buf, arena_size);

    json_value_t root;
    bool parse_ok = json_parse(file_buf, (size_t)file_size, &arena, &root);
    free(file_buf);
    if (!parse_ok || root.type != JSON_OBJECT) {
        free(arena_buf);
        return false;
    }

    /* Read version. */
    const json_value_t *ver_val = json_object_get(&root, "version");
    int version = jint_(ver_val, 0);
    if (version < 5) {
        /* Not a JSON-format fskel. */
        free(arena_buf);
        return false;
    }

    /* Read joints array. */
    const json_value_t *joints = json_object_get(&root, "joints");
    if (!joints || joints->type != JSON_ARRAY || joints->array.count == 0) {
        free(arena_buf);
        return false;
    }

    uint32_t joint_count = joints->array.count;
    if (joint_count > FSKEL_MAX_JOINTS) {
        free(arena_buf);
        return false;
    }

    /* Determine max constraints per joint. */
    uint32_t max_constraints = 0;
    for (uint32_t i = 0; i < joint_count; i++) {
        const json_value_t *j = &joints->array.items[i];
        if (j->type != JSON_OBJECT) continue;
        const json_value_t *cons = json_object_get(j, "constraints");
        if (cons && cons->type == JSON_ARRAY && cons->array.count > max_constraints) {
            max_constraints = cons->array.count;
        }
    }

    /* Initialize skeleton. */
    if (!skeleton_def_init(out_skel, joint_count, max_constraints)) {
        free(arena_buf);
        return false;
    }

    /* Parse each joint. */
    for (uint32_t i = 0; i < joint_count; i++) {
        const json_value_t *j = &joints->array.items[i];
        if (j->type != JSON_OBJECT) goto fail_skel;

        /* Name. */
        const json_value_t *name_val = json_object_get(j, "name");
        if (name_val && name_val->type == JSON_STRING) {
            uint32_t copy_len = name_val->string.len;
            if (copy_len >= SKELETON_JOINT_NAME_MAX)
                copy_len = SKELETON_JOINT_NAME_MAX - 1;
            memcpy(out_skel->joint_names[i], name_val->string.ptr, copy_len);
            out_skel->joint_names[i][copy_len] = '\0';
        }

        /* Parent index (-1 → UINT32_MAX). */
        out_skel->parent_indices[i] = juint_(json_object_get(j, "parent"), UINT32_MAX);

        /* Rest transforms. */
        jmat4_(json_object_get(j, "rest_local"), &out_skel->rest_local[i]);
        jmat4_(json_object_get(j, "rest_world"), &out_skel->rest_world[i]);

        /* Bone tail position (armature/engine space). */
        const json_value_t *tp = json_object_get(j, "tail_pos");
        if (tp && tp->type == JSON_ARRAY && tp->array.count >= 3) {
            if (!out_skel->tail_positions) {
                out_skel->tail_positions = (float *)calloc(
                    (size_t)joint_count * 3, sizeof(float));
                if (!out_skel->tail_positions) goto fail_skel;
            }
            out_skel->tail_positions[i * 3 + 0] = jfloat_(&tp->array.items[0], 0.0f);
            out_skel->tail_positions[i * 3 + 1] = jfloat_(&tp->array.items[1], 0.0f);
            out_skel->tail_positions[i * 3 + 2] = jfloat_(&tp->array.items[2], 0.0f);
        }

        /* Constraints. */
        const json_value_t *cons = json_object_get(j, "constraints");
        uint32_t nc = 0;
        if (cons && cons->type == JSON_ARRAY) {
            nc = cons->array.count;
            if (nc > max_constraints) nc = max_constraints;
            for (uint32_t ci = 0; ci < nc; ci++) {
                const json_value_t *cv = &cons->array.items[ci];
                if (cv->type != JSON_OBJECT) continue;

                constraint_def_t *def =
                    &out_skel->constraints[i * max_constraints + ci];
                memset(def, 0, sizeof(*def));

                int ct = constraint_type_from_string_(json_object_get(cv, "type"));
                if (ct < 0) continue;
                def->type = (constraint_type_t)ct;
                def->influence = jfloat_(json_object_get(cv, "influence"), 1.0f);
                def->owner_space = juint_(json_object_get(cv, "owner_space"), 0);
                def->target_space = juint_(json_object_get(cv, "target_space"), 0);
                def->target_bone_idx = juint_(json_object_get(cv, "target_bone"), UINT32_MAX);

                parse_constraint_params_(def, json_object_get(cv, "params"));
            }
        }
        out_skel->constraint_counts[i] = nc;

        /* Collider. */
        const json_value_t *col = json_object_get(j, "collider");
        if (col && col->type == JSON_OBJECT) {
            if (!out_skel->colliders) {
                out_skel->colliders = (bone_collider_desc_t *)calloc(
                    joint_count, sizeof(bone_collider_desc_t));
                if (!out_skel->colliders) goto fail_skel;
            }
            bone_collider_desc_t *cd = &out_skel->colliders[i];
            cd->shape_type = collider_shape_from_string_(json_object_get(col, "shape"));
            const json_value_t *params_arr = json_object_get(col, "params");
            if (params_arr && params_arr->type == JSON_ARRAY) {
                for (uint32_t pi = 0; pi < 6 && pi < params_arr->array.count; pi++) {
                    cd->params[pi] = jfloat_(&params_arr->array.items[pi], 0.0f);
                }
            }
            cd->ccd_enabled     = jbool_(json_object_get(col, "ccd"), false) ? 1 : 0;
            cd->is_kinematic    = jbool_(json_object_get(col, "kinematic"), false) ? 1 : 0;
            cd->mass            = jfloat_(json_object_get(col, "mass"), 0.0f);
            cd->hull_offset     = juint_(json_object_get(col, "hull_offset"), 0);
            cd->hull_count      = juint_(json_object_get(col, "hull_count"), 0);
            cd->collision_group = juint_(json_object_get(col, "collision_group"), 0);
        }

        /* Joint descriptor. */
        const json_value_t *jd = json_object_get(j, "joint_desc");
        if (jd && jd->type == JSON_OBJECT) {
            if (!out_skel->joints) {
                out_skel->joints = (bone_joint_desc_t *)calloc(
                    joint_count, sizeof(bone_joint_desc_t));
                if (!out_skel->joints) goto fail_skel;
            }
            bone_joint_desc_t *bd = &out_skel->joints[i];
            bd->joint_type  = juint_(json_object_get(jd, "type"), 0);
            const json_value_t *ax = json_object_get(jd, "axis");
            if (ax && ax->type == JSON_ARRAY && ax->array.count >= 3) {
                bd->axis[0] = jfloat_(&ax->array.items[0], 0.0f);
                bd->axis[1] = jfloat_(&ax->array.items[1], 1.0f);
                bd->axis[2] = jfloat_(&ax->array.items[2], 0.0f);
            }
            bd->rest_length = jfloat_(json_object_get(jd, "rest_length"), 0.0f);
            const json_value_t *lmin = json_object_get(jd, "limit_min");
            if (lmin && lmin->type == JSON_ARRAY && lmin->array.count >= 3) {
                bd->limit_min[0] = jfloat_(&lmin->array.items[0], 0.0f);
                bd->limit_min[1] = jfloat_(&lmin->array.items[1], 0.0f);
                bd->limit_min[2] = jfloat_(&lmin->array.items[2], 0.0f);
            }
            const json_value_t *lmax = json_object_get(jd, "limit_max");
            if (lmax && lmax->type == JSON_ARRAY && lmax->array.count >= 3) {
                bd->limit_max[0] = jfloat_(&lmax->array.items[0], 0.0f);
                bd->limit_max[1] = jfloat_(&lmax->array.items[1], 0.0f);
                bd->limit_max[2] = jfloat_(&lmax->array.items[2], 0.0f);
            }
            bd->limit_axes = juint_(json_object_get(jd, "limit_axes"), 0);
            /* Stiffness → compliance conversion: compliance = 1/stiffness.
             * Stiffness 0 means perfectly rigid (compliance = 0). */
            float stiffness = jfloat_(json_object_get(jd, "stiffness"), 0.0f);
            bd->compliance = (stiffness > 0.0f) ? (1.0f / stiffness) : 0.0f;
            bd->damping = jfloat_(json_object_get(jd, "damping"), 0.0f);
            bd->yield_strength = jfloat_(
                json_object_get(jd, "yield_strength"), 0.0f);
            bd->break_strength = jfloat_(
                json_object_get(jd, "break_strength"), 0.0f);

            /* Read explicit joint anchors (armature-space positions). */
            const json_value_t *anc_a = json_object_get(jd, "anchor_a");
            const json_value_t *anc_b = json_object_get(jd, "anchor_b");
            if (anc_a && anc_a->type == JSON_ARRAY && anc_a->array.count >= 3 &&
                anc_b && anc_b->type == JSON_ARRAY && anc_b->array.count >= 3) {
                bd->anchor_a[0] = jfloat_(&anc_a->array.items[0], 0.0f);
                bd->anchor_a[1] = jfloat_(&anc_a->array.items[1], 0.0f);
                bd->anchor_a[2] = jfloat_(&anc_a->array.items[2], 0.0f);
                bd->anchor_b[0] = jfloat_(&anc_b->array.items[0], 0.0f);
                bd->anchor_b[1] = jfloat_(&anc_b->array.items[1], 0.0f);
                bd->anchor_b[2] = jfloat_(&anc_b->array.items[2], 0.0f);
                bd->has_anchors = 1;
            }

            /* Drive flags and compliance for angular/linear drive. */
            bd->drive_flags = (uint8_t)jfloat_(
                json_object_get(jd, "drive_flags"), 0.0f);
            bd->drive_compliance = jfloat_(
                json_object_get(jd, "drive_compliance"), 0.0f);
        }
    }

    /* Read IBMs. */
    const json_value_t *ibms = json_object_get(&root, "ibms");
    if (ibms && ibms->type == JSON_ARRAY && ibms->array.count > 0 && out_ibms) {
        uint32_t ibm_count = ibms->array.count;
        *out_ibms = (mat4_t *)calloc(ibm_count, sizeof(mat4_t));
        if (!*out_ibms) goto fail_skel;
        for (uint32_t i = 0; i < ibm_count; i++) {
            jmat4_(&ibms->array.items[i], &(*out_ibms)[i]);
        }
        if (out_ibm_count) *out_ibm_count = ibm_count;
    } else {
        if (out_ibm_count) *out_ibm_count = 0;
        if (out_ibms) *out_ibms = NULL;
    }

    /* Read hull vertices. */
    const json_value_t *hulls = json_object_get(&root, "hull_vertices");
    if (hulls && hulls->type == JSON_ARRAY && hulls->array.count >= 3) {
        uint32_t hvc = hulls->array.count / 3;
        out_skel->hull_vertices = (float *)calloc(
            (size_t)hvc * 3, sizeof(float));
        if (!out_skel->hull_vertices) goto fail_skel;
        for (uint32_t i = 0; i < hvc * 3; i++) {
            out_skel->hull_vertices[i] = jfloat_(&hulls->array.items[i], 0.0f);
        }
        out_skel->hull_vertex_count = hvc;
    }

    free(arena_buf);
    return true;

fail_skel:
    skeleton_def_destroy(out_skel);
    free(arena_buf);
    return false;
}
