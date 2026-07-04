/**
 * @file grammar_blockout.h
 * @brief Blockout grammar — rasterizes token streams into dungeon layouts.
 *
 * This is the reference grammar. It handles 4/5-sided rooms,
 * axis-aligned and diagonal corridors, ramps, openings, spawn,
 * markers, and BLOCK/EBLOCK nesting.
 */

#ifndef FERRUM_PROCGEN_GRAMMAR_BLOCKOUT_H
#define FERRUM_PROCGEN_GRAMMAR_BLOCKOUT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ferrum/procgen/procgen_types.h"
#include "ferrum/procgen/procgen_layout.h"

/**
 * @brief Rasterize a blockout grammar token stream into a dungeon layout.
 *
 * @param tokens   Token stream from the tokenizer.
 * @param count    Number of tokens.
 * @param layout   Output layout (filled by this function).
 *                 Caller must allocate. Arrays inside are allocated
 *                 dynamically and caller must free them.
 * @param err_buf  Error message buffer.
 * @param err_cap  Error buffer capacity.
 * @return 0 on success, -1 on error.
 */
int grammar_blockout_rasterize(const procgen_token_t *tokens,
                               uint32_t count,
                               fr_dungeon_layout_t *layout,
                               char *err_buf, uint32_t err_cap);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_PROCGEN_GRAMMAR_BLOCKOUT_H */
