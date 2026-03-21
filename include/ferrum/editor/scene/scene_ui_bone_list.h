/**
 * @file scene_ui_bone_list.h
 * @brief Collapsible bone list in the inspector for regular edit mode.
 *
 * Shows bone names for entities with skeletons, allowing click-to-select
 * bones without entering prefab mode. Only shown when the selected entity
 * has a SCRIPT_KEY_SKEL_PATH attribute.
 *
 * Public types: none (0 / 2-type rule).
 */
#ifndef FERRUM_EDITOR_SCENE_UI_BONE_LIST_H
#define FERRUM_EDITOR_SCENE_UI_BONE_LIST_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* Forward declarations. */
struct scene_editor;

/**
 * @brief Build a collapsible bone list in the inspector panel.
 *
 * If the selected entity has a skeleton and we are NOT in prefab mode,
 * renders a "Bones" header followed by clickable bone name rows.
 * Clicking a bone selects it; shift+click toggles for multi-select.
 *
 * @param ed         Scene editor context (non-NULL).
 * @param entity_id  Entity to show bones for.
 * @param y_cursor   Pointer to current Y cursor in content space (updated).
 * @param scroll_px  Current scroll offset.
 * @param visible_h  Visible height of the inspector panel.
 * @param clay_idx   Pointer to Clay element index counter (updated).
 */
void scene_ui_build_bone_list(struct scene_editor *ed,
                               uint32_t entity_id,
                               float *y_cursor,
                               float scroll_px,
                               float visible_h,
                               uint32_t *clay_idx);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_SCENE_UI_BONE_LIST_H */
