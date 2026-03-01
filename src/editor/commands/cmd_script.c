/**
 * @file cmd_script.c
 * @brief Editor command handler for Aegis script management.
 *
 * Subcommands (via "action" arg):
 *   load   — compile IL source and register in script registry
 *   unload — unregister script by name
 *   list   — list registered scripts and their status
 *
 * Scripts are registered (compiled) but NOT immediately started.
 * They are lazily spawned when their subscribed topic fires.
 */

#include "ferrum/editor/edit_commands.h"
#include "ferrum/editor/edit_cmd_ctx.h"
#include "ferrum/editor/json_parse.h"
#include "ferrum/aegis/aegis_runtime.h"
#include "ferrum/aegis/aegis_asm.h"
#include <stdlib.h>
#include <string.h>

/**
 * @brief Handle "script" editor command.
 *
 * JSON args:
 *   {"action": "load", "name": "ai_patrol", "source": "..."}
 *   {"action": "unload", "name": "ai_patrol"}
 *   {"action": "list"}
 *
 * @return true on success, false on error.
 */
bool cmd_script(edit_dispatch_t *d, const json_value_t *args,
                json_value_t *result, json_arena_t *arena) {
    edit_cmd_ctx_t *ctx = (edit_cmd_ctx_t *)d->user_data;
    if (!ctx || !ctx->script_runtime || !args) return false;

    aegis_script_runtime_t *rt = ctx->script_runtime;

    /* Extract action string. */
    const json_value_t *action_val = json_object_get(args, "action");
    if (!action_val || action_val->type != JSON_STRING) return false;

    char action[32];
    if (!json_string_copy(action_val, action, sizeof(action))) return false;

    /* ---- LOAD ---- */
    if (strcmp(action, "load") == 0) {
        const json_value_t *name_val = json_object_get(args, "name");
        const json_value_t *src_val  = json_object_get(args, "source");
        if (!name_val || name_val->type != JSON_STRING) return false;
        if (!src_val  || src_val->type  != JSON_STRING) return false;

        char name[64];
        if (!json_string_copy(name_val, name, sizeof(name))) return false;

        /* Compile IL source. */
        aegis_asm_t as;
        memset(&as, 0, sizeof(as));
        aegis_bytecode_t bc;
        bool compiled = aegis_asm_compile(
            &as, src_val->string.ptr, src_val->string.len, &bc);

        if (!compiled) {
            /* Return compilation error string. */
            const char *err = aegis_asm_error(&as);
            size_t elen = strlen(err);
            char *ebuf = (char *)(arena->buf + arena->used);
            if (arena->used + elen + 1 > arena->cap) return false;
            memcpy(ebuf, err, elen + 1);
            arena->used += elen + 1;

            result->type = JSON_STRING;
            result->string.ptr = ebuf;
            result->string.len = (uint32_t)elen;
            return false;
        }

        /* Register in the runtime. */
        uint32_t reg_id = aegis_script_runtime_register(rt, name, &bc);
        free(bc.instructions);

        if (reg_id == AEGIS_SCRIPT_ID_INVALID) return false;

        result->type = JSON_NUMBER;
        result->number = (double)reg_id;
        return true;
    }

    /* ---- UNLOAD ---- */
    if (strcmp(action, "unload") == 0) {
        const json_value_t *name_val = json_object_get(args, "name");
        if (!name_val || name_val->type != JSON_STRING) return false;

        char name[64];
        if (!json_string_copy(name_val, name, sizeof(name))) return false;

        bool ok = aegis_script_runtime_unregister(rt, name);
        result->type = JSON_BOOL;
        result->boolean = ok;
        return ok;
    }

    /* ---- LIST ---- */
    if (strcmp(action, "list") == 0) {
        /* Count registered scripts. */
        uint32_t count = 0;
        for (uint32_t i = 0; i < AEGIS_REGISTRY_MAX; i++) {
            if (rt->registry[i].registered) count++;
        }

        /* Allocate array from arena. */
        json_value_t *elems = (json_value_t *)(arena->buf + arena->used);
        size_t needed = count * sizeof(json_value_t);
        if (arena->used + needed > arena->cap) return false;
        arena->used += needed;

        uint32_t idx = 0;
        for (uint32_t i = 0; i < AEGIS_REGISTRY_MAX && idx < count; i++) {
            if (!rt->registry[i].registered) continue;

            /* Build "name:status" string for each entry. */
            const char *status = rt->registry[i].spawned ? "active" : "registered";
            size_t nlen = strlen(rt->registry[i].name);
            size_t slen = strlen(status);
            size_t total = nlen + 1 + slen; /* "name:status" */

            char *str = (char *)(arena->buf + arena->used);
            if (arena->used + total + 1 > arena->cap) break;
            memcpy(str, rt->registry[i].name, nlen);
            str[nlen] = ':';
            memcpy(str + nlen + 1, status, slen);
            str[total] = '\0';
            arena->used += total + 1;

            elems[idx].type = JSON_STRING;
            elems[idx].string.ptr = str;
            elems[idx].string.len = (uint32_t)total;
            idx++;
        }

        result->type = JSON_ARRAY;
        result->array.items = elems;
        result->array.count = idx;
        return true;
    }

    return false;
}
