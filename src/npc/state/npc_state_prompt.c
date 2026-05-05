/**
 * @file npc_state_prompt.c
 * @brief Full prompt assembly from NPC state: system prompt, statblock,
 *        status line, awareness summary, context buffer, user message.
 *
 * Non-static functions (1 of 4 max):
 *   1. npc_state_prompt_assemble
 */

#include "ferrum/npc/npc_state_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void awareness_summary_(const npc_state_t *npc, char *buf,
                                size_t cap) {
    if (npc->awareness.count == 0) {
        snprintf(buf, cap, "Awareness: none\n");
        return;
    }
    size_t off = (size_t)snprintf(buf, cap, "Awareness:");
    for (uint32_t i = 0; i < npc->awareness.count && i < 5; i++) {
        char tmp[64];
        int n = snprintf(tmp, sizeof(tmp), " #%u(%.2f)",
                         npc->awareness.entries[i].entity_id,
                         npc->awareness.entries[i].last_salience);
        if (off + (size_t)n + 1 < cap) {
            memcpy(buf + off, tmp, (size_t)n + 1);
            off += (size_t)n;
        } else {
            break;
        }
    }
    if (off + 1 < cap) {
        buf[off] = '\n';
        buf[off + 1] = '\0';
    }
}

char *npc_state_prompt_assemble(const npc_state_t *npc,
                                const char *user_message) {
    if (!npc) return NULL;

    char aware_buf[512];
    awareness_summary_(npc, aware_buf, sizeof(aware_buf));

    size_t sys_len   = strlen(npc->system_prompt);
    size_t stat_len  = strlen(npc->statblock);
    size_t status_len = strlen(npc->status_line);
    size_t aware_len  = strlen(aware_buf);
    size_t user_len   = user_message ? strlen(user_message) : 0;

    const char *ctx_ptr = npc->context_buffer ? npc->context_buffer : "";
    size_t ctx_len      = npc->context_buffer ? npc->context_len : 0;

    if (npc->context_max_tokens > 0 && ctx_len > 0) {
        size_t max_chars = (size_t)npc->context_max_tokens * 4;

        size_t sep_sys   = sys_len > 0 ? 1 : 0;
        size_t sep_stat  = stat_len > 0 ? 1 : 0;
        size_t sep_status = status_len > 0 ? 1 : 0;
        size_t sep_ctx   = ctx_len > 0 ? 1 : 0;
        size_t sep_user  = user_len > 0 ? 1 : 0;

        size_t fixed = sys_len + sep_sys
                     + stat_len + sep_stat
                     + status_len + sep_status
                     + aware_len
                     + user_len + sep_user
                     + sep_ctx;

        if (fixed + ctx_len > max_chars) {
            if (max_chars > fixed) {
                size_t budget_for_ctx = max_chars - fixed;
                if (budget_for_ctx < ctx_len) {
                    ctx_ptr += (ctx_len - budget_for_ctx);
                    ctx_len = budget_for_ctx;
                }
            } else {
                ctx_len = 0;
            }
        }
    }

    size_t need = sys_len + stat_len + status_len + aware_len
                + ctx_len + user_len + 8;
    char *prompt = (char *)malloc(need + 1);
    if (!prompt) return NULL;

    size_t off = 0;
    if (sys_len > 0) {
        off += (size_t)snprintf(prompt + off, need - off, "%s\n",
                                npc->system_prompt);
    }
    if (stat_len > 0) {
        off += (size_t)snprintf(prompt + off, need - off, "%s\n",
                                npc->statblock);
    }
    if (status_len > 0) {
        off += (size_t)snprintf(prompt + off, need - off, "%s\n",
                                npc->status_line);
    }
    off += (size_t)snprintf(prompt + off, need - off, "%s", aware_buf);
    if (ctx_len > 0) {
        off += (size_t)snprintf(prompt + off, need - off, "%.*s\n",
                                (int)ctx_len, ctx_ptr);
    }
    if (user_len > 0) {
        off += (size_t)snprintf(prompt + off, need - off, "%s\n",
                                user_message);
    }

    return prompt;
}
