/**
 * @file frustum_cull.h
 * @brief Shared AABB-vs-frustum culling (rpg-0rs4).
 *
 * The exact "positive vertex" plane test that lived privately in
 * shadow_csm_render.c, hoisted so the forward pass, the depth pre-pass, and the
 * shadow draw loops can all skip geometry outside the view/light frustum. Pure
 * CPU math, no GL, no allocation, no owned state -- callers pass a column-major
 * matrix and per-renderable model + local AABB.
 *
 * Convention: every function returns nonzero when the object should be CULLED
 * (provably outside), zero when it must be kept (visible or conservatively so).
 * Ownership: none. Nullability: all pointers are required (no NULL checks in the
 * hot path -- callers guarantee validity). No side effects.
 */
#ifndef FERRUM_RENDERER_CULL_FRUSTUM_CULL_H
#define FERRUM_RENDERER_CULL_FRUSTUM_CULL_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Extract the 6 normalised frustum planes (Gribb-Hartmann) from a
 *        column-major view-projection (or light) matrix.
 * @param mvp    Column-major 4x4 matrix (proj*view), 16 floats.
 * @param planes Out: 6 planes, each (a,b,c,d) normalised so a*x+b*y+c*z+d is a
 *               signed distance (positive = inside).
 */
void frustum_extract_planes(const float mvp[16], float planes[6][4]);

/**
 * @brief As frustum_extract_planes, but from separate column-major projection
 *        and view matrices (computes proj*view internally). Lets callers that
 *        hold view/proj separately (render_camera_t) cull without linking the
 *        matrix library.
 * @param proj   Column-major view->clip, 16 floats.
 * @param view   Column-major world->view, 16 floats.
 * @param planes Out: 6 normalised planes (see frustum_extract_planes).
 */
void frustum_extract_planes_vp(const float proj[16], const float view[16],
                               float planes[6][4]);

/**
 * @brief Test a renderable's world AABB against the frustum planes.
 * @param planes 6 planes from frustum_extract_planes.
 * @param model  Column-major model->world, 16 floats.
 * @param lmin   Local AABB min (3 floats).
 * @param lmax   Local AABB max (3 floats).
 * @return 1 if the AABB is fully outside some plane (cull it); 0 to keep.
 */
int frustum_cull_aabb(const float planes[6][4], const float model[16],
                      const float lmin[3], const float lmax[3]);

/**
 * @brief As frustum_cull_aabb, plus a draw-distance cutoff: also cull when the
 *        world AABB's nearest point is farther than @p max_dist from @p eye.
 * @param eye      Camera world position (3 floats).
 * @param max_dist Far cull distance in world units; <= 0 disables the distance
 *                 test (frustum-only, identical to frustum_cull_aabb).
 * @return 1 to cull (outside frustum OR beyond max_dist); 0 to keep.
 */
int frustum_cull_aabb_ex(const float planes[6][4], const float model[16],
                         const float lmin[3], const float lmax[3],
                         const float eye[3], float max_dist);

/**
 * @brief Test a renderable's world AABB against a sphere (point-light range).
 * @param center Sphere centre / light position (3 floats).
 * @param radius Sphere radius / light range; <= 0 disables the test (never cull).
 * @param model  Column-major model->world, 16 floats.
 * @param lmin   Local AABB min (3 floats).
 * @param lmax   Local AABB max (3 floats).
 * @return 1 if the AABB lies entirely outside the sphere (cull it); 0 to keep.
 */
int sphere_cull_aabb(const float center[3], float radius, const float model[16],
                     const float lmin[3], const float lmax[3]);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_RENDERER_CULL_FRUSTUM_CULL_H */
