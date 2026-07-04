/**
 * @file procgen_grammar_registry.h
 * @brief Multi-grammar registry for procedural dungeon grammars.
 */

#ifndef FERRUM_PROCGEN_GRAMMAR_REGISTRY_H
#define FERRUM_PROCGEN_GRAMMAR_REGISTRY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "ferrum/procgen/procgen_types.h"
#include "ferrum/procgen/procgen_layout.h"

#define PROCGEN_MAX_GRAMMARS 16

/** @brief Function signature for grammar tokenizers. */
typedef tok_error_t (*procgen_tokenize_fn)(const char *input,
    procgen_token_t *tokens, uint32_t tok_cap,
    uint32_t *out_count, char *err_buf, uint32_t err_cap);

/** @brief Function signature for grammar rasterizers. */
typedef int (*procgen_rasterize_fn)(const procgen_token_t *tokens,
    uint32_t count, fr_dungeon_layout_t *layout,
    char *err_buf, uint32_t err_cap);

/**
 * @brief A compiled grammar module registered in the registry.
 */
typedef struct procgen_grammar {
    const char            *name;          /**< Grammar name (e.g., "blockout"). */
    uint32_t               version;       /**< Grammar version. */
    procgen_tokenize_fn    tokenize;      /**< Grammar-specific tokenizer. */
    procgen_rasterize_fn   rasterize;     /**< Grammar-specific rasterizer. */
    const char            *vlm_prompt;    /**< System prompt fragment for VLM. */
    const char           **known_markers; /**< Array of known marker names. */
    uint32_t               marker_count;  /**< Number of known markers. */
} procgen_grammar_t;

/**
 * @brief Initialize the grammar registry.
 */
void procgen_grammar_registry_init(void);

/**
 * @brief Clear all registered grammars.
 */
void procgen_grammar_registry_clear(void);

/**
 * @brief Register a grammar.
 *
 * @param grammar  Grammar descriptor (must be static or heap-allocated).
 * @return 0 on success, -1 if name already registered or registry full.
 */
int procgen_grammar_register(const procgen_grammar_t *grammar);

/**
 * @brief Find a grammar by name.
 *
 * @param name  Grammar name to look up.
 * @return Pointer to grammar, or NULL if not found.
 */
const procgen_grammar_t *procgen_grammar_find(const char *name);

/**
 * @brief Get the number of registered grammars.
 */
uint32_t procgen_grammar_count(void);

/**
 * @brief Rasterize a token stream using the grammar selected by
 *        the @grammar header token.
 *
 * Looks up the TOK_GRAMMAR token's name in the registry, then calls
 * that grammar's rasterize function with the remaining tokens.
 *
 * @param tokens   Token stream.
 * @param count    Token count.
 * @param layout   Output layout.
 * @param err_buf  Error buffer.
 * @param err_cap  Error buffer capacity.
 * @return 0 on success, -1 on error (unknown grammar, rasterize failure).
 */
int procgen_rasterize_with_registry(const procgen_token_t *tokens,
                                    uint32_t count,
                                    fr_dungeon_layout_t *layout,
                                    char *err_buf, uint32_t err_cap);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_PROCGEN_GRAMMAR_REGISTRY_H */
