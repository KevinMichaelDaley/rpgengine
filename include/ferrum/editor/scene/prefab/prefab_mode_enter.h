/**
 * @file prefab_mode_enter.h
 * @brief Enter and exit prefab editor mode.
 *
 * Entering prefab mode requires a single selected entity (any type).
 * All other entities not parented to the root are hidden. Exiting
 * restores visibility.
 *
 * Public types: none (0 / 2-type rule).
 */
#ifndef FERRUM_EDITOR_SCENE_PREFAB_MODE_ENTER_H
#define FERRUM_EDITOR_SCENE_PREFAB_MODE_ENTER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

/* Forward declaration. */
struct scene_editor;

/**
 * @brief Enter prefab editor mode.
 *
 * Validates that exactly one entity is selected. Hides all other
 * entities (except children of the root) and activates prefab mode.
 * Works with any entity type. If the entity has a skeleton, bone
 * collider editing features become available.
 *
 * @param ed  Scene editor (non-NULL).
 * @return true if prefab mode was activated, false on validation failure.
 */
bool prefab_mode_enter(struct scene_editor *ed);

/**
 * @brief Exit prefab editor mode.
 *
 * Restores hidden entities and deactivates prefab mode. If already
 * inactive, this is a no-op.
 *
 * @param ed  Scene editor (non-NULL).
 */
void prefab_mode_exit(struct scene_editor *ed);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_SCENE_PREFAB_MODE_ENTER_H */
