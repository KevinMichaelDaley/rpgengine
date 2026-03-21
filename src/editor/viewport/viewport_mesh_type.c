/**
 * @file viewport_mesh_type.c
 * @brief Per-entity mesh type transition validation.
 *
 * Non-static functions (1 / 4 limit):
 *   viewport_mesh_type_can_upgrade
 */

#include "ferrum/editor/viewport/viewport_mesh_type.h"

bool viewport_mesh_type_can_upgrade(viewport_mesh_type_t from,
                                     viewport_mesh_type_t to) {
    /* Same type is always allowed (no-op). */
    if (from == to) return true;

    /* Skeletal → anything else is FORBIDDEN (lossy, destructive). */
    if (from == VIEWPORT_MESH_SKELETAL) return false;

    /* NONE → STATIC, NONE → SKELETAL, STATIC → SKELETAL: all allowed. */
    /* STATIC → NONE: allowed (unloading). */
    return true;
}
