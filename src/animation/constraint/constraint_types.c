/**
 * @file constraint_types.c
 * @brief Constraint type name lookup and validation.
 */

#include "ferrum/animation/constraint_types.h"

/* ── Name table ──────────────────────────────────────────────────── */

static const char *s_type_names[CONSTRAINT_TYPE_COUNT] = {
    [CONSTRAINT_IK]              = "IK",
    [CONSTRAINT_SPLINE_IK]       = "Spline IK",
    [CONSTRAINT_CHILD_OF]        = "Child Of",
    [CONSTRAINT_COPY_TRANSFORMS] = "Copy Transforms",
    [CONSTRAINT_COPY_ROTATION]   = "Copy Rotation",
    [CONSTRAINT_COPY_LOCATION]   = "Copy Location",
    [CONSTRAINT_COPY_SCALE]      = "Copy Scale",
    [CONSTRAINT_DAMPED_TRACK]    = "Damped Track",
    [CONSTRAINT_TRACK_TO]        = "Track To",
    [CONSTRAINT_LOCKED_TRACK]    = "Locked Track",
    [CONSTRAINT_LIMIT_ROTATION]  = "Limit Rotation",
    [CONSTRAINT_LIMIT_LOCATION]  = "Limit Location",
    [CONSTRAINT_LIMIT_SCALE]     = "Limit Scale",
    [CONSTRAINT_TRANSFORMATION]  = "Transformation",
    [CONSTRAINT_ACTION]          = "Action",
    [CONSTRAINT_CLAMP_TO]        = "Clamp To",
    [CONSTRAINT_FLOOR]           = "Floor",
    [CONSTRAINT_MAINTAIN_VOLUME] = "Maintain Volume",
    [CONSTRAINT_SHRINKWRAP]      = "Shrinkwrap",
    [CONSTRAINT_PIVOT]           = "Pivot",
};

const char *constraint_type_name(constraint_type_t type) {
    if ((int)type < 0 || (int)type >= CONSTRAINT_TYPE_COUNT) {
        return "Unknown";
    }
    return s_type_names[type];
}

bool constraint_type_is_valid(constraint_type_t type) {
    return (int)type >= 0 && (int)type < CONSTRAINT_TYPE_COUNT;
}
