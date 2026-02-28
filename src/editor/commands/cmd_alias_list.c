/**
 * @file cmd_alias_list.c
 * @brief List all @ aliases (marker entities with @ prefix names).
 *
 * JSON args: {"pattern":"regex"} (optional, filters by name)
 * Returns JSON array: [{"name":"@foo","pos":[x,y,z],"rot":[rx,ry,rz]}, ...]
 */

#include "ferrum/editor/edit_commands.h"
#include "ferrum/editor/edit_cmd_ctx.h"
#include "ferrum/editor/edit_entity.h"

#include <regex.h>
#include <stdio.h>
#include <string.h>

bool cmd_alias_list(edit_dispatch_t *d, const json_value_t *args,
                    json_value_t *result, json_arena_t *arena) {
    edit_cmd_ctx_t *ctx = (edit_cmd_ctx_t *)d->user_data;
    if (!ctx || !ctx->entities) return false;

    /* Optional regex pattern filter. */
    regex_t re;
    bool has_pattern = false;
    if (args) {
        const json_value_t *pat_val = json_object_get(args, "pattern");
        if (pat_val && pat_val->type == JSON_STRING &&
            pat_val->string.len > 0) {
            char pat_buf[256];
            uint32_t len = pat_val->string.len < 255
                               ? pat_val->string.len : 255;
            memcpy(pat_buf, pat_val->string.ptr, len);
            pat_buf[len] = '\0';
            int rc = regcomp(&re, pat_buf,
                              REG_EXTENDED | REG_NOSUB | REG_ICASE);
            if (rc != 0) return false;
            has_pattern = true;
        }
    }

    /* Count matching @ entities first. */
    uint32_t match_count = 0;
    for (uint32_t i = 0; i < ctx->entities->capacity; i++) {
        const edit_entity_t *e = edit_entity_store_get(ctx->entities, i);
        if (!e || e->name[0] != '@') continue;
        if (has_pattern && regexec(&re, e->name, 0, NULL, 0) != 0) continue;
        match_count++;
    }

    /* Build result array in arena. */
    /* Each element is a JSON object with name, pos, rot. */
    size_t items_sz = match_count * sizeof(json_value_t);
    if (arena->used + items_sz > arena->cap) {
        if (has_pattern) regfree(&re);
        return false;
    }

    json_value_t *items = (json_value_t *)(arena->buf + arena->used);
    arena->used += items_sz;

    uint32_t idx = 0;
    for (uint32_t i = 0; i < ctx->entities->capacity && idx < match_count;
         i++) {
        const edit_entity_t *e = edit_entity_store_get(ctx->entities, i);
        if (!e || e->name[0] != '@') continue;
        if (has_pattern && regexec(&re, e->name, 0, NULL, 0) != 0) continue;

        /* Build object: {"name":"...", "pos":[...], "rot":[...]} */
        /* 3 keys: name, pos, rot */
        size_t obj_sz = 3 * sizeof(const char *) +
                        3 * sizeof(uint32_t) +
                        3 * sizeof(json_value_t);
        if (arena->used + obj_sz > arena->cap) break;

        const char **keys = (const char **)(arena->buf + arena->used);
        arena->used += 3 * sizeof(const char *);
        uint32_t *klens = (uint32_t *)(arena->buf + arena->used);
        arena->used += 3 * sizeof(uint32_t);
        json_value_t *vals = (json_value_t *)(arena->buf + arena->used);
        arena->used += 3 * sizeof(json_value_t);

        /* Key 0: name */
        keys[0] = "name"; klens[0] = 4;
        size_t name_len = strlen(e->name);
        if (arena->used + name_len > arena->cap) break;
        char *name_copy = (char *)(arena->buf + arena->used);
        memcpy(name_copy, e->name, name_len);
        arena->used += name_len;
        vals[0].type = JSON_STRING;
        vals[0].string.ptr = name_copy;
        vals[0].string.len = (uint32_t)name_len;

        /* Key 1: pos (as array of 3 numbers) */
        keys[1] = "pos"; klens[1] = 3;
        size_t arr_sz = 3 * sizeof(json_value_t);
        if (arena->used + arr_sz > arena->cap) break;
        json_value_t *pos_items = (json_value_t *)(arena->buf + arena->used);
        arena->used += arr_sz;
        for (int k = 0; k < 3; k++) {
            pos_items[k].type = JSON_NUMBER;
            pos_items[k].number = (double)e->pos[k];
        }
        vals[1].type = JSON_ARRAY;
        vals[1].array.items = pos_items;
        vals[1].array.count = 3;

        /* Key 2: rot (as array of 3 numbers) */
        keys[2] = "rot"; klens[2] = 3;
        if (arena->used + arr_sz > arena->cap) break;
        json_value_t *rot_items = (json_value_t *)(arena->buf + arena->used);
        arena->used += arr_sz;
        for (int k = 0; k < 3; k++) {
            rot_items[k].type = JSON_NUMBER;
            rot_items[k].number = (double)e->rot[k];
        }
        vals[2].type = JSON_ARRAY;
        vals[2].array.items = rot_items;
        vals[2].array.count = 3;

        /* Assemble object. */
        items[idx].type = JSON_OBJECT;
        items[idx].object.keys = keys;
        items[idx].object.key_lens = klens;
        items[idx].object.vals = vals;
        items[idx].object.count = 3;
        idx++;
    }

    if (has_pattern) regfree(&re);

    result->type = JSON_ARRAY;
    result->array.items = items;
    result->array.count = idx;
    return true;
}
