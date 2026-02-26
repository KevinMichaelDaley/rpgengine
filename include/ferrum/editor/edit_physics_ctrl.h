/**
 * @file edit_physics_ctrl.h
 * @brief Physics simulation control — pause, resume, step, reset.
 *
 * Callback interface that lets editor commands control the physics
 * simulation without depending on the tick runner directly.  The host
 * application (e.g., demo_server) provides implementations that map
 * to phys_tick_runner_pause/resume/step_once.
 *
 * Thread safety: callbacks are invoked from the editor drain thread
 * (the same thread that drains the command ring).
 */
#ifndef FERRUM_EDITOR_EDIT_PHYSICS_CTRL_H
#define FERRUM_EDITOR_EDIT_PHYSICS_CTRL_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Physics simulation control interface.
 *
 * All callbacks are optional (NULL = no-op).  The editor commands
 * check for NULL before invoking.
 */
typedef struct edit_physics_ctrl {
    /** @brief Pause the simulation.  Bodies freeze in place. */
    void (*on_pause)(void *user_data);

    /** @brief Resume the simulation from the current state. */
    void (*on_resume)(void *user_data);

    /**
     * @brief Advance exactly one physics tick while paused.
     *
     * Has no effect if physics is not paused — the command handler
     * checks is_paused() before calling this.
     */
    void (*on_step)(void *user_data);

    /**
     * @brief Reset physics state (zero all velocities).
     *
     * The host should also pause physics after reset.
     */
    void (*on_reset)(void *user_data);

    /**
     * @brief Query whether the simulation is currently paused.
     * @return true if paused, false if running.
     */
    bool (*is_paused)(void *user_data);

    /** @brief Opaque context passed to all callbacks. */
    void *user_data;
} edit_physics_ctrl_t;

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_EDIT_PHYSICS_CTRL_H */
