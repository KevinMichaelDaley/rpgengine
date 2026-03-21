/**
 * @file prefab_ui_outliner.h
 * @brief Prefab mode outliner panel rendering.
 *
 * Renders the bone hierarchy tree with nested colliders in the
 * outliner panel when prefab mode is active.
 *
 * Ownership: no ownership taken; reads editor state.
 * Nullability: all params must be non-NULL.
 * Side effects: emits Clay layout elements.
 *
 * Public types: none (0-type rule).
 */
#ifndef FERRUM_EDITOR_SCENE_PREFAB_UI_OUTLINER_H
#define FERRUM_EDITOR_SCENE_PREFAB_UI_OUTLINER_H

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations. */
struct scene_editor;
struct panel_rect;

/**
 * @brief Build the prefab mode outliner panel layout.
 *
 * Shows the skeleton bone hierarchy with nested collider entities.
 * Title bar shows "Prefab: <name>".
 *
 * @param ed    Scene editor context (non-NULL, must be in prefab mode).
 * @param rect  Panel screen rectangle (non-NULL).
 */
void scene_ui_build_prefab_outliner(struct scene_editor *ed,
                                    const struct panel_rect *rect);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_SCENE_PREFAB_UI_OUTLINER_H */
