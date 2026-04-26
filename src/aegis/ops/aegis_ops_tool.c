/**
 * @file aegis_ops_tool.c
 * @brief VM tool_action opcode: whitelist, dispatch, stub handlers.
 *
 * ≤4 non-static functions per file rule:
 *   1. aegis_op_tool_action (public)
 *   2. parse_json_args_key (static)
 *   3. allocate_result (static)
 *   4. tool_stub_not_implemented (static)
 *
 * Stubs for all 10 tools return "not yet implemented" until rpg-llm02a-d
 * provide real implementations.
 */

#include "ferrum/aegis/aegis_ops_tools.h"
#include "ferrum/aegis/aegis_tools.h"
#include "ferrum/aegis/aegis_vm.h"
#include "ferrum/aegis/aegis_decode.h"
#include "ferrum/aegis/aegis_memory.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define TOOL_RESULT_SLOT_SIZE 256
#define TOOL_FUEL_COST 50

/* ------------------------------------------------------------------ */
/* Static helpers                                                     */
/* ------------------------------------------------------------------ */

/**
 * @brief Parse a simple key from JSON args: looks for "key":"value".
 *
 * This is a minimal parser sufficient for the 0-1 argument tools.
 * It scans for the key and copies the quoted value into out_val.
 *
 * @param json     Null-terminated JSON string.
 * @param key      Key to search for.
 * @param out_val  Output buffer (must hold at least out_cap bytes).
 * @param out_cap  Capacity of out_val.
 * @return true if key found and value extracted.
 */
static bool parse_json_args_key(const char *json, const char *key,
                                char *out_val, size_t out_cap) {
    if (!json || !key || !out_val || out_cap == 0) {
        return false;
    }
    out_val[0] = '\0';

    size_t klen = strlen(key);
    const char *p = json;
    while (*p) {
        /* Find opening quote. */
        while (*p && *p != '"') p++;
        if (!*p) break;
        p++; /* skip quote */

        /* Check if this key matches. */
        if (strncmp(p, key, klen) == 0 && p[klen] == '"') {
            /* Found key — look for colon. */
            p += klen + 1;
            while (*p && (*p == ':' || isspace((unsigned char)*p))) p++;
            if (*p == '"') {
                p++;
                size_t i = 0;
                while (*p && *p != '"' && i + 1 < out_cap) {
                    out_val[i++] = *p++;
                }
                out_val[i] = '\0';
                return i > 0;
            }
            /* Handle unquoted values (numbers, booleans). */
            size_t i = 0;
            while (*p && *p != '}' && *p != ',' && i + 1 < out_cap) {
                if (!isspace((unsigned char)*p)) {
                    out_val[i++] = *p;
                }
                p++;
            }
            out_val[i] = '\0';
            return i > 0;
        }
        /* Skip to end of this key. */
        while (*p && *p != '"') p++;
        if (*p) p++;
    }
    return false;
}

/**
 * @brief Allocate a result slot in the heap arena and write status + message.
 *
 * @param vm       VM instance.
 * @param status   Status code (AEGIS_TOOL_OK or negative error).
 * @param msg      Null-terminated message text.
 * @return Heap offset of the result slot, or -1 on alloc failure.
 */
static int32_t allocate_result(aegis_vm_t *vm, int32_t status, const char *msg) {
    size_t msg_len = strlen(msg);
    size_t need = sizeof(int32_t) + msg_len + 1;
    if (need > TOOL_RESULT_SLOT_SIZE) {
        need = TOOL_RESULT_SLOT_SIZE;
    }

    int32_t off = aegis_memory_alloc(&vm->memory, (uint32_t)need);
    if (off < 0) {
        return -1;
    }

    uint8_t *dst = vm->memory.base + off;
    memcpy(dst, &status, sizeof(status));
    memcpy(dst + sizeof(status), msg, msg_len);
    dst[sizeof(status) + msg_len] = '\0';
    return off;
}

/**
 * @brief Stub handler for tools not yet implemented.
 *
 * All real tool handlers (TRADE_INIT, DEFEND, etc.) will be provided by
 * rpg-llm02a-d. Until then, every tool dispatches here.
 */
static bool tool_stub_not_implemented(aegis_vm_t *vm,
                                      aegis_tool_id_t tool_id,
                                      const char *args_json) {
    (void)args_json;

    const char *names[] = {
        "TRADE_INIT", "TRADE_SELL", "TRADE_BUY",
        "TRADE_ACCEPT", "TRADE_REJECT",
        "DEFEND", "ATTACK", "FLEE", "GOTO",
        "KNOWLEDGE_QUERY"
    };
    const char *name = (tool_id < AEGIS_TOOL_COUNT) ? names[tool_id] : "UNKNOWN";

    char msg[192];
    snprintf(msg, sizeof(msg), "%s: not yet implemented", name);

    int32_t off = allocate_result(vm, AEGIS_TOOL_OK, msg);
    if (off < 0) {
        return false;
    }
    vm->regs[0].i32 = off; /* result offset */
    return true;
}

/* ------------------------------------------------------------------ */
/* Public opcode handler                                              */
/* ------------------------------------------------------------------ */

bool aegis_op_tool_action(aegis_vm_t *vm, const aegis_decode_result_t *d) {
    /* Read tool_id from register B. */
    int32_t tool_id = vm->regs[d->raw_b].i32;
    if (tool_id < 0 || tool_id >= AEGIS_TOOL_COUNT) {
        int32_t off = allocate_result(vm, AEGIS_TOOL_UNKNOWN,
                                      "Unknown tool. Available tools: TRADE_INIT, TRADE_SELL, TRADE_BUY, TRADE_ACCEPT, TRADE_REJECT, DEFEND, ATTACK, FLEE, GOTO, KNOWLEDGE_QUERY.");
        if (off >= 0) {
            vm->regs[d->raw_a].i32 = off;
        }
        return false;
    }

    /* Read args JSON from heap at offset in register C. */
    int32_t args_off = vm->regs[d->raw_c].i32;
    const char *args_json = "";
    if (args_off >= 0 && (uint32_t)args_off < vm->memory.arena_size) {
        args_json = (const char *)(vm->memory.base + args_off);
        /* Safety: ensure null-terminated within arena bounds. */
        uint32_t max_len = vm->memory.arena_size - (uint32_t)args_off;
        uint32_t alen = 0;
        while (alen < max_len && args_json[alen] != '\0') {
            alen++;
        }
        if (alen >= max_len) {
            args_json = "";
        }
    }

    /* Deduct fuel. */
    if (vm->fuel > TOOL_FUEL_COST) {
        vm->fuel -= TOOL_FUEL_COST;
    } else {
        vm->fuel = 0;
    }

    /* Dispatch to stub (real handlers come in rpg-llm02a-d). */
    return tool_stub_not_implemented(vm, (aegis_tool_id_t)tool_id, args_json);
}
