#ifndef FERRUM_PROCGEN_ASCII_PARSE_H
#define FERRUM_PROCGEN_ASCII_PARSE_H

#include <stdint.h>
#include "ferrum/procgen/procgen_srd_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Parse a multi-floor ASCII floor plan string into a RoomGraph.
 *
 * @param ascii  Null-terminated string containing one or more
 *               "=== FLOOR N: label ===" blocks, each followed by
 *               lines of space-separated characters.
 * @param graph  Output graph.  Must be uninitialized (fresh stack
 *               alloc) — the function calls fr_room_graph_init internally.
 * @return 0 on success, -1 on parse error.
 */
int procgen_ascii_parse(const char *ascii, fr_room_graph_t *graph);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_PROCGEN_ASCII_PARSE_H */
