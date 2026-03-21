/**
 * @file scene_ui_skel_promote.h
 * @brief Inspector skeleton promotion section for MESH entities.
 *
 * Builds a gear-button + skeleton assignment UI using the generic
 * asset_ref_widget. When confirmed, sets SCRIPT_KEY_SKEL_PATH on
 * the entity and triggers skeleton loading.
 *
 * No public types (0 / 2-type rule).
 */
#ifndef FERRUM_EDITOR_SCENE_UI_SKEL_PROMOTE_H
#define FERRUM_EDITOR_SCENE_UI_SKEL_PROMOTE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* Forward declarations. */
struct scene_editor;

/**
 * @brief Build the skeleton promotion section in the inspector.
 *
 * For MESH entities, renders a gear button that expands to show
 * a skeleton assignment widget (asset_ref_widget filtered to
 * EDIT_ASSET_SKELETON).
 *
 * @param ed          Scene editor context (non-NULL).
 * @param entity_id   Entity to show promotion for.
 * @param y_cursor    Y cursor position (logical px, updated on return).
 * @param scroll_px   Inspector scroll offset.
 * @param visible_h   Visible height of inspector panel.
 * @param clay_idx    Clay ID offset for unique IDs.
 * @return Height consumed by this section (0 if not a MESH entity).
 */
float scene_ui_build_skel_promote(struct scene_editor *ed,
                                    uint32_t entity_id,
                                    float y_cursor,
                                    int scroll_px,
                                    float visible_h,
                                    int clay_idx);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_SCENE_UI_SKEL_PROMOTE_H */
