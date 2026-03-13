/**
 * @file mode_object.h
 * @brief Object mode operations — translate, rotate, scale,
 *        duplicate, delete selected entities.
 *
 * Operates on the entity store using the current selection.
 * All operations apply to every entity in the selection.
 *
 * Ownership: does not take ownership of store or selection.
 * Nullability: all pointer params must be non-NULL.
 * Error semantics: empty selection = no-op.
 * Side effects: mutates entity transforms and store.
 *
 * Public types: none (functions only, uses existing types).
 */
#ifndef FERRUM_EDITOR_MODE_OBJECT_H
#define FERRUM_EDITOR_MODE_OBJECT_H

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations. */
struct edit_entity_store;
struct edit_selection;

/**
 * @brief Translate all selected entities by delta[3].
 * @param store  Entity store (non-NULL).
 * @param sel    Selection (non-NULL).
 * @param delta  Translation delta [x, y, z].
 */
void object_mode_translate(struct edit_entity_store *store,
                            const struct edit_selection *sel,
                            const float delta[3]);

/**
 * @brief Rotate all selected entities by delta[3] (Euler degrees).
 * @param store  Entity store (non-NULL).
 * @param sel    Selection (non-NULL).
 * @param delta  Rotation delta [pitch, yaw, roll] in degrees.
 */
void object_mode_rotate(struct edit_entity_store *store,
                          const struct edit_selection *sel,
                          const float delta[3]);

/**
 * @brief Scale all selected entities (multiply current scale).
 * @param store  Entity store (non-NULL).
 * @param sel    Selection (non-NULL).
 * @param delta  Scale factors [sx, sy, sz].
 */
void object_mode_scale(struct edit_entity_store *store,
                        const struct edit_selection *sel,
                        const float delta[3]);

/**
 * @brief Duplicate all selected entities. Selection updates to duplicates.
 * @param store  Entity store (non-NULL).
 * @param sel    Selection (non-NULL, mutated to contain duplicates).
 */
void object_mode_duplicate(struct edit_entity_store *store,
                             struct edit_selection *sel);

/**
 * @brief Delete all selected entities and clear the selection.
 * @param store  Entity store (non-NULL).
 * @param sel    Selection (non-NULL, cleared after delete).
 */
void object_mode_delete(struct edit_entity_store *store,
                          struct edit_selection *sel);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_MODE_OBJECT_H */
