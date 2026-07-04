/**
 * @file procgen_tokenize.h
 * @brief Tokenizer for procedural dungeon grammar strings.
 *
 * Parses a null-terminated grammar string into a flat stream of
 * procgen_token_t structs with line/col error reporting.
 */

#ifndef FERRUM_PROCGEN_TOKENIZE_H
#define FERRUM_PROCGEN_TOKENIZE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "ferrum/procgen/procgen_types.h"

/**
 * @brief Tokenize a null-terminated grammar string into a token stream.
 *
 * @param input    Null-terminated input string (e.g. "@grammar blockout v1\n...")
 * @param tokens   Output buffer for parsed tokens.
 * @param tok_cap  Maximum number of tokens that fit in tokens[].
 * @param out_count On success, set to the number of tokens emitted.
 * @param err_buf  Buffer for error message (set on failure).
 * @param err_cap  Size of err_buf in bytes.
 * @return 0 on success, -1 on parse error (err_buf populated).
 */
int procgen_tokenize(const char *input,
                     procgen_token_t *tokens, uint32_t tok_cap,
                     uint32_t *out_count,
                     char *err_buf, uint32_t err_cap);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_PROCGEN_TOKENIZE_H */
