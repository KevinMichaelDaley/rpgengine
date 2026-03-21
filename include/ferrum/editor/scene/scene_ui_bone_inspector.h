/**
 * @file scene_ui_bone_inspector.h
 * @brief Bone property display for the inspector panel.
 *
 * When bones are selected in the viewport, renders bone properties
 * (head position, tail position, collider shape, mass) into the
 * inspector panel below the entity properties section.
 *
 * Public types: none (0 / 2-type rule).
 */
#ifndef FERRUM_EDITOR_SCENE_UI_BONE_INSPECTOR_H
#define FERRUM_EDITOR_SCENE_UI_BONE_INSPECTOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* Forward declarations. */
struct scene_editor;
struct panel_rect;

/**
 * @brief Build bone inspector Clay UI elements.
 *
 * If bones are selected and the active entity has a skeleton, this
 * appends bone property rows to the inspector panel. Otherwise it is
 * a no-op.
 *
 * @param ed        Scene editor context (non-NULL).
 * @param y_cursor  Pointer to current Y cursor in content space (updated).
 * @param scroll_px Current scroll offset.
 * @param visible_h Visible height of the inspector panel.
 * @param clay_idx  Pointer to Clay element index counter (updated).
 */
void scene_ui_build_bone_inspector(struct scene_editor *ed,
                                    float *y_cursor,
                                    float scroll_px,
                                    float visible_h,
                                    uint32_t *clay_idx);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_SCENE_UI_BONE_INSPECTOR_H */
