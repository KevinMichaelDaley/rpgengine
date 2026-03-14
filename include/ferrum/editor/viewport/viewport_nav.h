/**
 * @file viewport_nav.h
 * @brief Per-viewport navigation mode definitions.
 *
 * Navigation mode controls how mouse/keyboard input drives the camera:
 *   - Orbit Selection: orbit around selection centroid (default)
 *   - Orbit Cursor: orbit around 3D cursor position
 *   - Fly: free mouselook + WASD movement, no pivot
 *   - Pan-Zoom: 2D-style pan and zoom only, no orbit
 *
 * Ownership: pure value types, no resources.
 * Nullability: N/A (enum + inline helpers).
 * Error semantics: N/A.
 * Side effects: none.
 *
 * Public types: nav_mode_t (1 / 2-type rule).
 */
#ifndef FERRUM_EDITOR_VIEWPORT_NAV_H
#define FERRUM_EDITOR_VIEWPORT_NAV_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Navigation mode for a viewport.
 */
typedef enum nav_mode {
    NAV_MODE_ORBIT_SELECTION = 0, /**< Orbit around selection centroid. */
    NAV_MODE_ORBIT_CURSOR    = 1, /**< Orbit around 3D cursor. */
    NAV_MODE_FLY             = 2, /**< Free mouselook + WASD. */
    NAV_MODE_PAN_ZOOM        = 3, /**< Pan and zoom only. */
    NAV_MODE_COUNT           = 4
} nav_mode_t;

/** @brief Get display name for a nav mode. */
static inline const char *nav_mode_name(nav_mode_t mode) {
    switch (mode) {
    case NAV_MODE_ORBIT_SELECTION: return "Orbit";
    case NAV_MODE_ORBIT_CURSOR:   return "OrbCur";
    case NAV_MODE_FLY:            return "Fly";
    case NAV_MODE_PAN_ZOOM:       return "PanZm";
    default:                      return "?";
    }
}

/** @brief Cycle to the next nav mode (wraps around). */
static inline nav_mode_t nav_mode_next(nav_mode_t mode) {
    return (nav_mode_t)(((uint8_t)mode + 1) % NAV_MODE_COUNT);
}

/** @brief Whether this mode uses orbit controls. */
static inline bool nav_mode_allows_orbit(nav_mode_t mode) {
    return mode == NAV_MODE_ORBIT_SELECTION || mode == NAV_MODE_ORBIT_CURSOR;
}

/** @brief Whether this mode uses fly controls. */
static inline bool nav_mode_allows_fly(nav_mode_t mode) {
    return mode == NAV_MODE_FLY;
}

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_VIEWPORT_NAV_H */
