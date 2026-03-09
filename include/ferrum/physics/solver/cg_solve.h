#ifndef FERRUM_PHYSICS_SOLVER_CG_SOLVE_H
#define FERRUM_PHYSICS_SOLVER_CG_SOLVE_H

/** @file
 * @brief Sparse projected Conjugate Gradient solver for joint constraints.
 *
 * Solves the constraint-space linear system A·Δλ = b with per-row
 * box constraints λ_min ≤ λ ≤ λ_max using projected CG with Jacobi
 * preconditioning.
 */

#include <stdbool.h>
#include <stdint.h>

#include "ferrum/physics/solver/cg_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations. */
struct phys_frame_arena;
struct phys_island;
struct phys_constraint;
struct phys_body;
struct phys_joint;
struct phys_mat3;
struct phys_velocity;

/**
 * @brief Allocate CG workspace from the frame arena.
 *
 * Allocates all arrays in the cg_system_t for up to max_rows rows.
 * Returns false if the arena cannot satisfy the allocation.
 *
 * @param sys       System to initialize (non-NULL).
 * @param arena     Frame arena for allocation (non-NULL).
 * @param max_rows  Maximum number of constraint rows.
 * @return true on success, false on allocation failure.
 *
 * @note Ownership: all allocations owned by the arena.
 */
bool phys_cg_alloc(cg_system_t *sys,
                   struct phys_frame_arena *arena,
                   uint32_t max_rows);

/**
 * @brief Assemble the sparse system matrix A and RHS from island joints.
 *
 * Collects all joint constraint rows from the island, builds the
 * sparse J·M⁻¹·Jᵀ matrix with XPBD regularization and geometric
 * stiffness, and computes the RHS vector.
 *
 * Contact constraints (is_joint == 0) are skipped.
 *
 * @param sys              CG system workspace (must be allocated).
 * @param island           Island to assemble.
 * @param constraints      Global constraint array.
 * @param bodies           Body array (positions, orientations).
 * @param inv_inertia_world World-space inverse inertia per body.
 * @param velocities       Current velocity workspace.
 * @param body_count       Total body count.
 * @param dt               Substep timestep.
 *
 * @note Sets sys->overflow if any row exceeds CG_MAX_NNZ_PER_ROW.
 */
void phys_cg_assemble(cg_system_t *sys,
                      const struct phys_island *island,
                      const struct phys_constraint *constraints,
                      const struct phys_body *bodies,
                      const struct phys_mat3 *inv_inertia_world,
                      const struct phys_velocity *velocities,
                      uint32_t body_count,
                      float dt);

/**
 * @brief Run the projected CG solver.
 *
 * Solves A·λ = b subject to λ_min ≤ λ ≤ λ_max using Jacobi-
 * preconditioned projected CG.  The solution is written to sys->lambda.
 *
 * @param sys        Assembled CG system.
 * @param max_iters  Maximum CG iterations.
 * @param tolerance  Convergence threshold on ||r||².
 * @return Number of iterations performed.
 */
uint32_t phys_cg_solve(cg_system_t *sys,
                       uint32_t max_iters,
                       float tolerance);

/**
 * @brief Apply CG solution to velocities and body positions.
 *
 * Computes Δv = M⁻¹·Jᵀ·Δλ for each body and updates velocities
 * and positions (for coupled integration).
 *
 * @param sys              CG system with solved lambda.
 * @param island           Island being solved.
 * @param constraints      Global constraint array (lambdas written back).
 * @param bodies_mut       Mutable body array (positions/orientations updated).
 * @param inv_inertia_world World-space inverse inertia per body.
 * @param velocities       Velocity workspace (updated).
 * @param body_count       Total body count.
 * @param dt               Substep timestep.
 * @param joints           Joint array (for anchor lookup). May be NULL.
 * @param joint_count      Number of joints.
 * @param constraint_joint_indices Maps constraint → joint index. May be NULL.
 */
void phys_cg_apply(const cg_system_t *sys,
                   const struct phys_island *island,
                   struct phys_constraint *constraints,
                   struct phys_body *bodies_mut,
                   const struct phys_mat3 *inv_inertia_world,
                   struct phys_velocity *velocities,
                   uint32_t body_count,
                   float dt,
                   const struct phys_joint *joints,
                   uint32_t joint_count,
                   const uint32_t *constraint_joint_indices);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_PHYSICS_SOLVER_CG_SOLVE_H */
