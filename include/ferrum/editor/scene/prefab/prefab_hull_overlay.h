/**
 * @file prefab_hull_overlay.h
 * @brief Draw convex hull wireframes from the hull cache.
 *
 * Renders hull edges as GL_LINES for visual feedback during
 * prefab editing.
 *
 * Ownership: no ownership taken; reads cache data.
 * Nullability: all pointer params must be non-NULL.
 * Side effects: emits GL draw calls.
 *
 * Public types: none (0-type rule).
 */
#ifndef FERRUM_EDITOR_SCENE_PREFAB_HULL_OVERLAY_H
#define FERRUM_EDITOR_SCENE_PREFAB_HULL_OVERLAY_H

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations. */
struct viewport_render_state;
struct prefab_hull_cache;
struct mat4;

/**
 * @brief Draw wireframe hulls from the cache.
 *
 * @param state   Viewport render state (non-NULL).
 * @param cache   Hull cache (non-NULL).
 * @param view    View matrix.
 * @param proj    Projection matrix.
 */
void prefab_hull_overlay_draw(struct viewport_render_state *state,
                              const struct prefab_hull_cache *cache,
                              const struct mat4 *view,
                              const struct mat4 *proj);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_SCENE_PREFAB_HULL_OVERLAY_H */
