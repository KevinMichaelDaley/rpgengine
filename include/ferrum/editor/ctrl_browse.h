/**
 * @file ctrl_browse.h
 * @brief Browse result cache — numbered references for asset browsing.
 *
 * Stores the last browse result set so users can reference items by
 * number (#1, #2, etc.) in subsequent commands.
 *
 * Public types: 1 (ctrl_browse_t).
 * Ownership: caller owns the struct; init/destroy manage internal memory.
 */
#ifndef FERRUM_EDITOR_CTRL_BROWSE_H
#define FERRUM_EDITOR_CTRL_BROWSE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/** Maximum number of browse results cached. */
#define CTRL_BROWSE_MAX_RESULTS 100

/** Maximum path length for a single browse result. */
#define CTRL_BROWSE_PATH_MAX    256

/**
 * @brief Browse result cache.
 *
 * Stores up to CTRL_BROWSE_MAX_RESULTS asset paths from the most recent
 * browse command. Results are numbered 1..count for #N reference syntax.
 *
 * Thread safety: not thread-safe — accessed only from TUI thread.
 */
typedef struct ctrl_browse {
    char     paths[CTRL_BROWSE_MAX_RESULTS][CTRL_BROWSE_PATH_MAX];
    uint32_t count;     /**< Number of valid entries (0..MAX_RESULTS). */
} ctrl_browse_t;

/**
 * @brief Initialize a browse cache (zeroes everything).
 * @param[out] browse  Cache to initialize. Must not be NULL.
 */
void ctrl_browse_init(ctrl_browse_t *browse);

/**
 * @brief Clear all browse results.
 * @param[in,out] browse  Cache to clear. Must not be NULL.
 */
void ctrl_browse_clear(ctrl_browse_t *browse);

/**
 * @brief Store a browse result set (replaces any previous results).
 *
 * Copies up to CTRL_BROWSE_MAX_RESULTS paths from the given array.
 *
 * @param[in,out] browse  Cache to populate. Must not be NULL.
 * @param[in]     paths   Array of null-terminated path strings.
 * @param[in]     count   Number of paths in the array.
 */
void ctrl_browse_set(ctrl_browse_t *browse, const char *const *paths,
                     uint32_t count);

/**
 * @brief Expand a #N reference to the corresponding asset path.
 *
 * If @p token starts with '#' followed by a decimal number N (1-based),
 * and N is within the cached result range, returns the corresponding path.
 *
 * @param[in] browse  Cache to query. Must not be NULL.
 * @param[in] token   Token to expand (e.g., "#3"). Must not be NULL.
 * @return Pointer to the cached path string, or NULL if not a valid #N ref.
 *
 * @note The returned pointer is valid until the next ctrl_browse_set() or
 *       ctrl_browse_clear() call.
 */
const char *ctrl_browse_expand(const ctrl_browse_t *browse, const char *token);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_CTRL_BROWSE_H */
