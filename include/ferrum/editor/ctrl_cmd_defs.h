/**
 * @file ctrl_cmd_defs.h
 * @brief TUI command definitions — names, help text, argument parsing.
 *
 * Provides a static table of known commands for help display,
 * tab completion, and text-to-JSON argument conversion.
 */
#ifndef FERRUM_EDITOR_CTRL_CMD_DEFS_H
#define FERRUM_EDITOR_CTRL_CMD_DEFS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Argument format descriptor for a TUI command.
 *
 * Specifies how to convert space-separated tokens into JSON args.
 * Format string tokens (space-separated):
 *   "s:name"   — string arg named "name"
 *   "f:name"   — single float named "name"
 *   "f3:name"  — three floats as array named "name"
 *   "u:name"   — unsigned integer named "name"
 *   "b:name"   — boolean named "name" (true/false)
 */
typedef struct ctrl_cmd_def {
    const char *name;       /**< Command name (e.g., "spawn"). */
    const char *alias;      /**< Short alias (e.g., "sp"), or NULL. */
    const char *usage;      /**< Usage string (e.g., "spawn <type> <x> <y> <z>"). */
    const char *help;       /**< One-line help text. */
    const char *arg_fmt;    /**< Argument format (NULL = no args). */
} ctrl_cmd_def_t;

/**
 * @brief Get the static command definition table.
 * @param[out] count  Number of entries in the table.
 * @return Pointer to the first entry.
 */
const ctrl_cmd_def_t *ctrl_cmd_defs_table(uint32_t *count);

/**
 * @brief Look up a command definition by name.
 * @param name  Command name (null-terminated).
 * @return Pointer to definition, or NULL if not found.
 */
const ctrl_cmd_def_t *ctrl_cmd_defs_find(const char *name);

/**
 * @brief Build a JSON command string from user text input.
 *
 * Splits the input text on whitespace, looks up the command definition,
 * and converts arguments to proper JSON format.
 *
 * @param input   User-typed text (e.g., "spawn box 0 5 0").
 * @param out     Output buffer for JSON string.
 * @param out_cap Capacity of output buffer.
 * @param cmd_id  Monotonically increasing command ID.
 * @return Number of bytes written (0 on error).
 */
uint32_t ctrl_cmd_build_json(const char *input, char *out, uint32_t out_cap,
                             uint32_t cmd_id);

/**
 * @brief Get tab-completion candidates for a partial command.
 *
 * @param prefix    Partial text typed so far.
 * @param matches   Array of pointers to matching command names (out).
 * @param max_matches  Capacity of matches array.
 * @return Number of matches found.
 */
uint32_t ctrl_cmd_complete(const char *prefix, const char **matches,
                           uint32_t max_matches);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_CTRL_CMD_DEFS_H */
