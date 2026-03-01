/**
 * @file edit_script_api.h
 * @brief Safe engine API for sandboxed Lua scripts.
 *
 * Provides rate-limited logging (engine.log/warn/err) and validated
 * entity write (engine.write_entity) as the ONLY side-effect endpoints
 * available to scripts. Replaces print, rawset, rawget, setmetatable,
 * getmetatable which are stripped during registration.
 *
 * Thread safety: used only on the script thread.
 * Ownership: log_state and env are borrowed (caller-owned).
 */
#ifndef FERRUM_EDITOR_EDIT_SCRIPT_API_H
#define FERRUM_EDITOR_EDIT_SCRIPT_API_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* Forward declarations. */
struct lua_State;
struct script_env;

/**
 * @brief Rate-limiting state for script logging.
 *
 * Uses a simple counter per tick. The counter is decremented on each log
 * call; when it reaches zero, further calls return false.
 *
 * Reset via script_log_state_reset() at the start of each tick.
 */
typedef struct script_log_state {
    uint32_t remaining;    /**< Calls remaining this tick. */
    uint32_t max_per_tick; /**< Maximum calls per tick. */
} script_log_state_t;

/**
 * @brief Initialize the log rate-limiter.
 *
 * @param state         State to initialize. Must not be NULL.
 * @param max_per_tick  Maximum log calls per tick.
 *
 * Side effects: sets remaining = max_per_tick.
 */
void script_log_state_init(script_log_state_t *state, uint32_t max_per_tick);

/**
 * @brief Reset the log rate-limiter for a new tick.
 *
 * @param state  State to reset. Must not be NULL.
 *
 * Side effects: restores remaining to max_per_tick.
 */
void script_log_state_reset(script_log_state_t *state);

/**
 * @brief Register the engine API table on a Lua state.
 *
 * Creates the global "engine" table with:
 *   - engine.log(msg)   → rate-limited stdout
 *   - engine.warn(msg)  → rate-limited stderr
 *   - engine.err(msg)   → rate-limited stderr
 *   - engine.write_entity(id, key, type_str, value)
 *
 * Also strips additional dangerous globals: print, rawset, rawget,
 * setmetatable, getmetatable.
 *
 * @param L          Lua state (must have sandbox already applied).
 * @param env        Script environment for entity writes. Must not be NULL.
 * @param log_state  Rate limiter. Must not be NULL. Must outlive L.
 *
 * Side effects: creates "engine" global, removes dangerous globals.
 * Ownership: env and log_state are borrowed; caller must keep alive.
 */
void script_api_register(struct lua_State *L, struct script_env *env,
                         script_log_state_t *log_state);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_EDIT_SCRIPT_API_H */
