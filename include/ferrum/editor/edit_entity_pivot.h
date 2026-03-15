/**
 * @file edit_entity_pivot.h
 * @brief Pivot-offset utility for computing entity geometry center.
 *
 * The entity's `pos` field represents the pivot world position.
 * Geometry is offset by -pivot_offset in local space. The geometry
 * center in world space is: pos + R * diag(scale) * (-pivot_offset).
 *
 * Public types: none (0 / 2-type rule).
 */
#ifndef FERRUM_EDITOR_EDIT_ENTITY_PIVOT_H
#define FERRUM_EDITOR_EDIT_ENTITY_PIVOT_H

#ifdef __cplusplus
extern "C" {
#endif

struct edit_entity;

/**
 * @brief Compute the world-space geometry center of an entity.
 *
 * When pivot_offset is zero, the geometry center equals entity.pos.
 * Otherwise: out = pos + rotate(orientation, scale * (-pivot_offset)).
 *
 * @param ent  Entity (non-NULL).
 * @param out  Output position [3] (non-NULL).
 */
void edit_entity_geometry_center(const struct edit_entity *ent, float out[3]);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_EDIT_ENTITY_PIVOT_H */
