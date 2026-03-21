/**
 * @file edit_entity_types.c
 * @brief Entity type registry — built-in types and name lookup.
 *
 * Non-static functions: edit_entity_type_registry, edit_entity_type_by_name (2).
 */

#include "ferrum/editor/edit_entity.h"
#include <string.h>

/* ── Built-in type registry ───────────────────────────────────────── */

static const edit_entity_type_info_t s_types[] = {
    { .name = "box",       .type_id = EDIT_ENTITY_TYPE_BOX },
    { .name = "sphere",    .type_id = EDIT_ENTITY_TYPE_SPHERE },
    { .name = "capsule",   .type_id = EDIT_ENTITY_TYPE_CAPSULE },
    { .name = "marker",    .type_id = EDIT_ENTITY_TYPE_MARKER },
    { .name = "mesh",      .type_id = EDIT_ENTITY_TYPE_MESH },
    { .name = "halfspace", .type_id = EDIT_ENTITY_TYPE_HALFSPACE },
    /* Collider-only types: invisible physics bodies. */
    { .name = "collider_sphere",  .type_id = EDIT_ENTITY_TYPE_COLLIDER_SPHERE },
    { .name = "collider_box",     .type_id = EDIT_ENTITY_TYPE_COLLIDER_BOX },
    { .name = "collider_capsule", .type_id = EDIT_ENTITY_TYPE_COLLIDER_CAPSULE },
    { .name = "collider_hull",    .type_id = EDIT_ENTITY_TYPE_COLLIDER_HULL },
};

static const uint32_t s_type_count =
    (uint32_t)(sizeof(s_types) / sizeof(s_types[0]));

const edit_entity_type_info_t *edit_entity_type_registry(uint32_t *count) {
    if (count) *count = s_type_count;
    return s_types;
}

uint32_t edit_entity_type_by_name(const char *name) {
    if (!name) return UINT32_MAX;
    for (uint32_t i = 0; i < s_type_count; i++) {
        if (strcmp(s_types[i].name, name) == 0) {
            return s_types[i].type_id;
        }
    }
    return UINT32_MAX;
}
