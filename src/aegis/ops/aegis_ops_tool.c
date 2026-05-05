/**
 * @file aegis_ops_tool.c
 * @brief VM tool_action opcode: whitelist, dispatch, real handlers.
 *
 * ≤4 non-static functions per file rule:
 *   1. aegis_op_tool_action (public)
 *   2. parse_json_args_key (static)
 *   3. allocate_result (static)
 *
 * Every tool ID has an explicit handler — no stub fallthrough.
 */

#include "ferrum/aegis/aegis_ops_tools.h"
#include "ferrum/aegis/aegis_tools.h"
#include "ferrum/aegis/aegis_vm.h"
#include "ferrum/aegis/aegis_decode.h"
#include "ferrum/aegis/aegis_memory.h"
#include "ferrum/npc/npc_knowledge_graph.h"
#include "ferrum/npc/npc_trade.h"
#include "ferrum/npc/npc_nav_action.h"
#include "ferrum/physics/phys_vec3.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
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
 * @brief Error handler for combat tools (DEFEND, ATTACK, FLEE).
 *
 * Returns AEGIS_TOOL_UNKNOWN with a clear message so the LLM
 * can distinguish "engine error" from "operation succeeded".
 */
static bool handle_combat_not_available(aegis_vm_t *vm,
                                        const char *args_json) {
    (void)args_json;

    int32_t off = allocate_result(vm, AEGIS_TOOL_UNKNOWN,
                                   "Combat system not available");
    if (off < 0) {
        return false;
    }
    vm->regs[0].i32 = off;
    return true;
}

/**
 * @brief Real handler for GOTO (tool_id = 8).
 *
 * Parses "target" and optional (x,y,z) from JSON args, then dispatches
 * to npc_nav_action_goto() for validation and async nav submission.
 */
static bool handle_goto(aegis_vm_t *vm, const char *args_json) {
    char target_name[64] = {0};
    parse_json_args_key(args_json, "target", target_name, sizeof(target_name));

    char x_str[32] = {0}, y_str[32] = {0}, z_str[32] = {0};
    parse_json_args_key(args_json, "x", x_str, sizeof(x_str));
    parse_json_args_key(args_json, "y", y_str, sizeof(y_str));
    parse_json_args_key(args_json, "z", z_str, sizeof(z_str));

    phys_vec3_t target_pos = {0.0f, 0.0f, 0.0f};
    if (x_str[0]) target_pos.x = (float)atof(x_str);
    if (y_str[0]) target_pos.y = (float)atof(y_str);
    if (z_str[0]) target_pos.z = (float)atof(z_str);

    char result_buf[256];
    result_buf[0] = '\0';

    bool success = npc_nav_action_goto(NULL, 0, NULL,
                                        target_name[0] ? target_name : NULL,
                                        target_pos,
                                        false, false,
                                        vm->async_buffer,
                                        result_buf, sizeof(result_buf));

    int32_t status = success ? AEGIS_TOOL_OK : AEGIS_TOOL_NAV;
    int32_t off = allocate_result(vm, status, result_buf);
    if (off < 0) return false;
    vm->regs[0].i32 = off;
    return true;
}

/* ------------------------------------------------------------------ */
/* Trade tool handlers (static)                                        */
/* ------------------------------------------------------------------ */

/** Static barter states for entity 1 (caller) and entity 2 (target). */
static npc_barter_state_t g_trade_state_a;
static npc_barter_state_t g_trade_state_b;
static bool g_trade_initialized;

/**
 * @brief Handle TRADE_INIT (tool_id = 0).
 *
 * Resets barter states and initiates trade between entity 1 and 2.
 */
static bool handle_trade_init(aegis_vm_t *vm, const char *args_json) {
    (void)args_json;

    npc_barter_state_init(&g_trade_state_a);
    npc_barter_state_init(&g_trade_state_b);

    uint64_t now_us = 0; /* Simplified: no real clock in VM context. */
    int err = npc_trade_init(&g_trade_state_a, &g_trade_state_b, 1, 2, now_us);

    const char *msg = npc_trade_error_str(err);
    if (err == NPC_TRADE_OK) {
        g_trade_initialized = true;
    }

    int32_t off = allocate_result(vm, (err == NPC_TRADE_OK) ? AEGIS_TOOL_OK : err, msg);
    if (off < 0) return false;
    vm->regs[0].i32 = off;
    return true;
}

/**
 * @brief Handle TRADE_SELL (tool_id = 1).
 *
 * Parses optional "item" from JSON args. In trade: sets offer.
 * Not in trade: returns broadcast message.
 */
static bool handle_trade_sell(aegis_vm_t *vm, const char *args_json) {
    char item_name[64] = {0};
    parse_json_args_key(args_json, "item", item_name, sizeof(item_name));

    npc_barter_state_t *their = g_trade_initialized ? &g_trade_state_b : NULL;
    uint32_t item_id = (item_name[0] != '\0') ? 1 : 0;

    int err = npc_trade_sell(&g_trade_state_a, their, item_id);

    char msg[256];
    if (err == NPC_TRADE_BROADCAST) {
        snprintf(msg, sizeof(msg), "Broadcasting sell intent for %s within 10m",
                 item_name[0] ? item_name : "unnamed item");
    } else if (err == NPC_TRADE_OK) {
        snprintf(msg, sizeof(msg), "Offer set: %s", item_name[0] ? item_name : "item #1");
    } else {
        snprintf(msg, sizeof(msg), "%s", npc_trade_error_str(err));
    }

    int32_t status = (err >= 0) ? AEGIS_TOOL_OK : err;
    int32_t off = allocate_result(vm, status, msg);
    if (off < 0) return false;
    vm->regs[0].i32 = off;
    return true;
}

/**
 * @brief Handle TRADE_BUY (tool_id = 2).
 *
 * Parses optional "item" from JSON args. In trade: sets want.
 * Not in trade: returns broadcast message.
 */
static bool handle_trade_buy(aegis_vm_t *vm, const char *args_json) {
    char item_name[64] = {0};
    parse_json_args_key(args_json, "item", item_name, sizeof(item_name));

    npc_barter_state_t *their = g_trade_initialized ? &g_trade_state_b : NULL;
    uint32_t item_id = (item_name[0] != '\0') ? 1 : 0;

    int err = npc_trade_buy(&g_trade_state_a, their, item_id);

    char msg[256];
    if (err == NPC_TRADE_BROADCAST) {
        snprintf(msg, sizeof(msg), "Broadcasting buy intent for %s within 10m",
                 item_name[0] ? item_name : "unnamed item");
    } else if (err == NPC_TRADE_OK) {
        snprintf(msg, sizeof(msg), "Want set: %s", item_name[0] ? item_name : "item #1");
    } else {
        snprintf(msg, sizeof(msg), "%s", npc_trade_error_str(err));
    }

    int32_t status = (err >= 0) ? AEGIS_TOOL_OK : err;
    int32_t off = allocate_result(vm, status, msg);
    if (off < 0) return false;
    vm->regs[0].i32 = off;
    return true;
}

/**
 * @brief Handle TRADE_ACCEPT (tool_id = 3).
 *
 * Validates both parties are ACTIVE and resolves.
 */
static bool handle_trade_accept(aegis_vm_t *vm, const char *args_json) {
    (void)args_json;

    int err = npc_trade_accept(&g_trade_state_a, &g_trade_state_b);
    const char *msg = npc_trade_error_str(err);

    int32_t status = (err == NPC_TRADE_OK) ? AEGIS_TOOL_OK : err;
    int32_t off = allocate_result(vm, status, msg);
    if (off < 0) return false;
    vm->regs[0].i32 = off;
    return true;
}

/**
 * @brief Handle TRADE_REJECT (tool_id = 4).
 *
 * Resolves both parties to BARTER_RESOLVED.
 */
static bool handle_trade_reject(aegis_vm_t *vm, const char *args_json) {
    (void)args_json;

    int err = npc_trade_reject(&g_trade_state_a, &g_trade_state_b);
    const char *msg = npc_trade_error_str(err);

    int32_t status = (err == NPC_TRADE_OK) ? AEGIS_TOOL_OK : err;
    int32_t off = allocate_result(vm, status, msg);
    if (off < 0) return false;
    vm->regs[0].i32 = off;
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
                                       "Unknown tool. Available tools: TRADE_INIT, TRADE_SELL, TRADE_BUY, TRADE_ACCEPT, TRADE_REJECT, DEFEND, ATTACK, FLEE, GOTO, KNOWLEDGE_QUERY, RELATED_ENTITIES, KG_SHORTEST_PATH.");
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

    /* Dispatch to real handler or stub. */
    if (tool_id == AEGIS_TOOL_KNOWLEDGE_QUERY) {
        return aegis_op_knowledge_query(vm, args_json);
    }
    if (tool_id == AEGIS_TOOL_RELATED_ENTITIES) {
        return aegis_op_related_entities(vm, args_json);
    }
    if (tool_id == AEGIS_TOOL_KG_SHORTEST_PATH) {
        return aegis_op_kg_path(vm, args_json);
    }
    switch (tool_id) {
    case AEGIS_TOOL_TRADE_INIT:   return handle_trade_init(vm, args_json);
    case AEGIS_TOOL_TRADE_SELL:   return handle_trade_sell(vm, args_json);
    case AEGIS_TOOL_TRADE_BUY:    return handle_trade_buy(vm, args_json);
    case AEGIS_TOOL_TRADE_ACCEPT: return handle_trade_accept(vm, args_json);
    case AEGIS_TOOL_TRADE_REJECT: return handle_trade_reject(vm, args_json);
    case AEGIS_TOOL_DEFEND:
    case AEGIS_TOOL_ATTACK:
    case AEGIS_TOOL_FLEE:         return handle_combat_not_available(vm, args_json);
    case AEGIS_TOOL_GOTO:         return handle_goto(vm, args_json);
    default: return false;
    }
}
