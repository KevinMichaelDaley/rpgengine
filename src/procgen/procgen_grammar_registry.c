/**
 * @file procgen_grammar_registry.c
 * @brief Multi-grammar registry implementation.
 */

#include "ferrum/procgen/procgen_grammar_registry.h"
#include "ferrum/procgen/procgen_tokenize.h"

#include <stdio.h>
#include <string.h>

static const procgen_grammar_t *g_grammars[PROCGEN_MAX_GRAMMARS];
static uint32_t g_grammar_count = 0;
static int g_initialized = 0;

void procgen_grammar_registry_init(void) {
    if (!g_initialized) {
        memset(g_grammars, 0, sizeof(g_grammars));
        g_grammar_count = 0;
        g_initialized = 1;
    }
}

void procgen_grammar_registry_clear(void) {
    memset(g_grammars, 0, sizeof(g_grammars));
    g_grammar_count = 0;
}

int procgen_grammar_register(const procgen_grammar_t *grammar) {
    if (!grammar || !grammar->name) return -1;

    if (!g_initialized) procgen_grammar_registry_init();

    /* Check for duplicates. */
    for (uint32_t i = 0; i < g_grammar_count; i++) {
        if (strcmp(g_grammars[i]->name, grammar->name) == 0)
            return -1;
    }

    if (g_grammar_count >= PROCGEN_MAX_GRAMMARS) return -1;

    g_grammars[g_grammar_count++] = grammar;
    return 0;
}

const procgen_grammar_t *procgen_grammar_find(const char *name) {
    if (!name) return NULL;
    if (!g_initialized) procgen_grammar_registry_init();

    for (uint32_t i = 0; i < g_grammar_count; i++) {
        if (strcmp(g_grammars[i]->name, name) == 0)
            return g_grammars[i];
    }
    return NULL;
}

uint32_t procgen_grammar_count(void) {
    if (!g_initialized) return 0;
    return g_grammar_count;
}

int procgen_rasterize_with_registry(const procgen_token_t *tokens,
                                    uint32_t count,
                                    fr_dungeon_layout_t *layout,
                                    char *err_buf, uint32_t err_cap) {
    if (!tokens || !layout || count == 0) {
        if (err_buf && err_cap > 0)
            snprintf(err_buf, err_cap, "invalid arguments");
        return -1;
    }

    /* Find the @grammar token. */
    const procgen_token_t *grammar_tok = NULL;
    for (uint32_t i = 0; i < count; i++) {
        if (tokens[i].type == TOK_GRAMMAR) {
            grammar_tok = &tokens[i];
            break;
        }
    }
    if (!grammar_tok) {
        if (err_buf && err_cap > 0)
            snprintf(err_buf, err_cap, "missing @grammar header in token stream");
        return -1;
    }

    /* Look up grammar in registry. */
    const procgen_grammar_t *grammar =
        procgen_grammar_find(grammar_tok->value.s);
    if (!grammar) {
        if (err_buf && err_cap > 0)
            snprintf(err_buf, err_cap,
                     "unknown grammar '%s' (not registered)",
                     grammar_tok->value.s);
        return -1;
    }

    /* Version check. */
    if (grammar->version != grammar_tok->grammar_version) {
        if (err_buf && err_cap > 0)
            snprintf(err_buf, err_cap,
                     "grammar '%s' version mismatch: requested v%u, "
                     "registered v%u",
                     grammar->name,
                     grammar_tok->grammar_version,
                     grammar->version);
        /* Non-fatal warning — continue despite version mismatch. */
    }

    /* Call the grammar's rasterize function. */
    if (!grammar->rasterize) {
        if (err_buf && err_cap > 0)
            snprintf(err_buf, err_cap,
                     "grammar '%s' has no rasterize function",
                     grammar->name);
        return -1;
    }

    return grammar->rasterize(tokens, count, layout, err_buf, err_cap);
}
