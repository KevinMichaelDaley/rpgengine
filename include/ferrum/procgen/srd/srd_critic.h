/**
 * @file srd_critic.h
 * @brief Swappable critic interface for the SRD optimiser.
 *
 * The SRD loop holds an srd_critic_t* and never needs to know which
 * backend is active. Two backends are provided:
 *
 *   - **AnalyticalCritic**: hand-crafted differentiable losses using
 *     libtorch ops. No .pt file required.
 *   - **TorchScriptCritic**: loads a compiled TorchScript model at
 *     runtime from a .pt path.
 *
 * **C callers** see only the opaque srd_critic_t and the three C API
 * functions (create_analytical, create_torchscript, destroy).
 *
 * **C++ callers** see the full ISrdCritic interface, AnalyticalCritic,
 * and TorchScriptCritic classes.
 *
 * @note No libtorch headers are included in the C-visible portion.
 *       The C++ section is guarded by __cplusplus.
 */
#ifndef FERRUM_PROCGEN_SRD_CRITIC_H
#define FERRUM_PROCGEN_SRD_CRITIC_H

#ifdef __cplusplus
extern "C" {
#endif

/* ── C API (opaque pointer) ────────────────────────────────────── */

/**
 * @brief Opaque critic handle.
 *
 * Created by srd_critic_create_analytical or srd_critic_create_torchscript.
 * Destroyed by srd_critic_destroy.
 *
 * @note Ownership: caller owns the returned pointer. Thread safety:
 *       score() is not thread-safe; use one critic per thread.
 */
typedef struct srd_critic srd_critic_t;

/**
 * @brief Create an AnalyticalCritic with default weights.
 *
 * Uses hand-crafted differentiable loss terms (NonPenetration,
 * MinimumSize, TypeSeparation, AdjacencyCount, SoftReachability,
 * BoundsViolation) with default weights from AnalyticalCritic::Config.
 *
 * @param layout_w  Layout world width (for BoundsViolation term).
 * @param layout_h  Layout world height (for BoundsViolation term).
 * @return New critic handle, or NULL on failure.
 *
 * @note Ownership: caller must call srd_critic_destroy when done.
 * @note Side effects: allocates heap memory.
 */
srd_critic_t *srd_critic_create_analytical(float layout_w, float layout_h);

/**
 * @brief Create a TorchScriptCritic from a compiled .pt model.
 *
 * The model must accept (params: Tensor[N,4], types: Tensor[N]) and
 * return a scalar Tensor.
 *
 * @param pt_path  Path to the compiled TorchScript model file.
 * @return New critic handle, or NULL if the file cannot be loaded.
 *
 * @note Ownership: caller must call srd_critic_destroy when done.
 * @note Side effects: reads from disk; allocates heap memory.
 */
srd_critic_t *srd_critic_create_torchscript(const char *pt_path);

/**
 * @brief Destroy a critic and free its resources.
 *
 * @param c  Critic to destroy. NULL is a safe no-op.
 */
void srd_critic_destroy(srd_critic_t *c);

#ifdef __cplusplus
} /* extern "C" */

/* ── C++ API (full interface) ──────────────────────────────────── */

#include <torch/torch.h>

namespace ferrum::srd {

/**
 * @brief Abstract base for all critic implementations.
 *
 * The SRD loop holds an ISrdCritic* and never knows which backend
 * is active. Subclasses must implement score(), which returns a
 * scalar loss tensor that is differentiable w.r.t. layout_params.
 */
class ISrdCritic {
public:
    virtual ~ISrdCritic() = default;

    /**
     * @brief Score a layout.
     *
     * @param layout_params  [N, 4] float tensor — (cx, cz, hw, hd) per box.
     *                       Must have requires_grad = true for gradient flow.
     * @param layout_types   [N] int64 tensor — srd_room_type_t per box.
     * @return Scalar float tensor (lower = better). Must be differentiable
     *         w.r.t. layout_params.
     *
     * @note Side effects: none beyond autograd graph construction.
     */
    virtual torch::Tensor score(torch::Tensor layout_params,
                                torch::Tensor layout_types) = 0;
};

/**
 * @brief Analytical critic with hand-crafted differentiable losses.
 *
 * All loss terms are implemented using libtorch ops, requiring no
 * .pt file. Loss terms:
 *   - **NonPenetration**: sum of soft SDF overlaps via smooth-min
 *   - **MinimumSize**: max(min_size - hw, 0)^2 + max(min_size - hd, 0)^2
 *   - **TypeSeparation**: penalise boss/treasure adjacent to entrance
 *   - **AdjacencyCount**: per-type target degree penalty
 *   - **SoftReachability**: softmax Dijkstra surrogate for connectivity
 *   - **BoundsViolation**: penalise boxes outside layout boundary
 */
class AnalyticalCritic : public ISrdCritic {
public:
    /**
     * @brief Configuration for AnalyticalCritic loss term weights.
     *
     * All weights are non-negative. Larger values increase the
     * importance of the corresponding loss term.
     */
    struct Config {
        float min_room_size   = 1.0f;   /**< Minimum room half-extent */
        float min_corridor_w  = 0.5f;   /**< Minimum corridor half-width */
        float layout_w        = 20.0f;  /**< Layout world width */
        float layout_h        = 20.0f;  /**< Layout world height */
        float w_penetration   = 1.0f;   /**< Weight: NonPenetration */
        float w_min_size      = 0.5f;   /**< Weight: MinimumSize */
        float w_separation    = 0.3f;   /**< Weight: TypeSeparation */
        float w_adjacency     = 0.2f;   /**< Weight: AdjacencyCount */
        float w_reachability  = 1.0f;   /**< Weight: SoftReachability */
        float w_bounds        = 2.0f;   /**< Weight: BoundsViolation */
    };

    /**
     * @brief Construct an AnalyticalCritic.
     *
     * @param cfg  Configuration with loss weights and size constraints.
     *             Defaults are used if omitted.
     */
    explicit AnalyticalCritic(const Config &cfg = {});

    torch::Tensor score(torch::Tensor params,
                        torch::Tensor types) override;

private:
    Config cfg_;
};

/**
 * @brief TorchScript critic: loads a compiled .pt model at runtime.
 *
 * The model must accept (params: Tensor[N,4], types: Tensor[N]) and
 * return a scalar Tensor[]. Swap at startup by passing a different
 * srd_critic_t* to the SRD loop — no code changes required.
 */
class TorchScriptCritic : public ISrdCritic {
public:
    /**
     * @brief Load a TorchScript model from disk.
     *
     * @param pt_path  Path to the .pt file.
     * @throws std::runtime_error if the file cannot be loaded.
     */
    explicit TorchScriptCritic(const char *pt_path);

    torch::Tensor score(torch::Tensor params,
                        torch::Tensor types) override;

private:
    torch::jit::script::Module module_;
};

} /* namespace ferrum::srd */

#endif /* __cplusplus */
#endif /* FERRUM_PROCGEN_SRD_CRITIC_H */
