/**
 * @file cmd_mesh_create.c
 * @brief Mesh creation commands: mesh_create_box, mesh_create_sphere,
 *        mesh_create_cylinder, mesh_create_plane.
 *
 * JSON args:
 *   mesh_create_box:      {"size":[w,h,d], "segments":[sx,sy,sz], "pos":[x,y,z]}
 *   mesh_create_sphere:   {"radius":r, "segments":n, "pos":[x,y,z]}
 *   mesh_create_cylinder: {"radius":r, "height":h, "segments":n, "axis":0|1|2, "pos":[x,y,z]}
 *   mesh_create_plane:    {"size":[w,h], "segments":[sx,sy], "axis":0|1|2, "pos":[x,y,z]}
 */

#include "ferrum/editor/edit_commands.h"
#include "ferrum/editor/edit_cmd_ctx.h"
#include "ferrum/editor/mesh/mesh_edit.h"
#include "ferrum/editor/mesh/mesh_primitives.h"

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static bool extract_vec3_(const json_value_t *arr, float out[3]) {
    if (!arr || arr->type != JSON_ARRAY || arr->array.count < 3) return false;
    for (int i = 0; i < 3; i++) {
        if (arr->array.items[i].type != JSON_NUMBER) return false;
        out[i] = (float)arr->array.items[i].number;
    }
    return true;
}

static bool extract_uvec3_(const json_value_t *arr, uint32_t out[3]) {
    if (!arr || arr->type != JSON_ARRAY || arr->array.count < 3) return false;
    for (int i = 0; i < 3; i++) {
        if (arr->array.items[i].type != JSON_NUMBER) return false;
        out[i] = (uint32_t)arr->array.items[i].number;
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* mesh_create_box                                                     */
/* ------------------------------------------------------------------ */

bool cmd_mesh_create_box(edit_dispatch_t *d, const json_value_t *args,
                         json_value_t *result, json_arena_t *arena) {
    (void)arena;
    edit_cmd_ctx_t *ctx = (edit_cmd_ctx_t *)d->user_data;
    if (!ctx || !ctx->mesh) return false;

    mesh_slot_t *slot = mesh_edit_get_active_slot(ctx->mesh);
    if (!slot) return false;

    /* Clear existing geometry in active slot */
    mesh_slot_clear(slot);

    float size[3] = {1, 1, 1};
    uint32_t segs[3] = {1, 1, 1};
    float pos[3] = {0, 0, 0};

    if (args) {
        extract_vec3_(json_object_get(args, "size"), size);
        extract_uvec3_(json_object_get(args, "segments"), segs);
        extract_vec3_(json_object_get(args, "pos"), pos);
    }

    mesh_prim_box(slot, size, segs, pos);

    result->type = JSON_NULL; (void)arena;
    return true;
}

/* ------------------------------------------------------------------ */
/* mesh_create_sphere                                                  */
/* ------------------------------------------------------------------ */

bool cmd_mesh_create_sphere(edit_dispatch_t *d, const json_value_t *args,
                            json_value_t *result, json_arena_t *arena) {
    (void)arena;
    edit_cmd_ctx_t *ctx = (edit_cmd_ctx_t *)d->user_data;
    if (!ctx || !ctx->mesh) return false;

    mesh_slot_t *slot = mesh_edit_get_active_slot(ctx->mesh);
    if (!slot) return false;

    mesh_slot_clear(slot);

    float radius = 1.0f;
    uint32_t segments = 16;
    float pos[3] = {0, 0, 0};

    if (args) {
        const json_value_t *r = json_object_get(args, "radius");
        if (r && r->type == JSON_NUMBER) radius = (float)r->number;
        const json_value_t *s = json_object_get(args, "segments");
        if (s && s->type == JSON_NUMBER) segments = (uint32_t)s->number;
        extract_vec3_(json_object_get(args, "pos"), pos);
    }

    mesh_prim_sphere(slot, radius, segments, pos);

    result->type = JSON_NULL; (void)arena;
    return true;
}

/* ------------------------------------------------------------------ */
/* mesh_create_cylinder                                                */
/* ------------------------------------------------------------------ */

bool cmd_mesh_create_cylinder(edit_dispatch_t *d, const json_value_t *args,
                              json_value_t *result, json_arena_t *arena) {
    (void)arena;
    edit_cmd_ctx_t *ctx = (edit_cmd_ctx_t *)d->user_data;
    if (!ctx || !ctx->mesh) return false;

    mesh_slot_t *slot = mesh_edit_get_active_slot(ctx->mesh);
    if (!slot) return false;

    mesh_slot_clear(slot);

    float radius = 1.0f, height = 2.0f;
    uint32_t segments = 16;
    int axis = 1; /* Y-up default */
    float pos[3] = {0, 0, 0};

    if (args) {
        const json_value_t *r = json_object_get(args, "radius");
        if (r && r->type == JSON_NUMBER) radius = (float)r->number;
        const json_value_t *h = json_object_get(args, "height");
        if (h && h->type == JSON_NUMBER) height = (float)h->number;
        const json_value_t *s = json_object_get(args, "segments");
        if (s && s->type == JSON_NUMBER) segments = (uint32_t)s->number;
        const json_value_t *a = json_object_get(args, "axis");
        if (a && a->type == JSON_NUMBER) axis = (int)a->number;
        extract_vec3_(json_object_get(args, "pos"), pos);
    }

    mesh_prim_cylinder(slot, radius, height, segments, axis, pos);

    result->type = JSON_NULL; (void)arena;
    return true;
}

/* ------------------------------------------------------------------ */
/* mesh_create_plane                                                   */
/* ------------------------------------------------------------------ */

bool cmd_mesh_create_plane(edit_dispatch_t *d, const json_value_t *args,
                           json_value_t *result, json_arena_t *arena) {
    (void)arena;
    edit_cmd_ctx_t *ctx = (edit_cmd_ctx_t *)d->user_data;
    if (!ctx || !ctx->mesh) return false;

    mesh_slot_t *slot = mesh_edit_get_active_slot(ctx->mesh);
    if (!slot) return false;

    mesh_slot_clear(slot);

    float size[2] = {1, 1};
    uint32_t segs[2] = {1, 1};
    int axis = 1;
    float pos[3] = {0, 0, 0};

    if (args) {
        const json_value_t *sz = json_object_get(args, "size");
        if (sz && sz->type == JSON_ARRAY && sz->array.count >= 2) {
            size[0] = (float)sz->array.items[0].number;
            size[1] = (float)sz->array.items[1].number;
        }
        const json_value_t *sg = json_object_get(args, "segments");
        if (sg && sg->type == JSON_ARRAY && sg->array.count >= 2) {
            segs[0] = (uint32_t)sg->array.items[0].number;
            segs[1] = (uint32_t)sg->array.items[1].number;
        }
        const json_value_t *a = json_object_get(args, "axis");
        if (a && a->type == JSON_NUMBER) axis = (int)a->number;
        extract_vec3_(json_object_get(args, "pos"), pos);
    }

    mesh_prim_plane(slot, size, segs, axis, pos);

    result->type = JSON_NULL; (void)arena;
    return true;
}
