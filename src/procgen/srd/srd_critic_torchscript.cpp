/**
 * @file srd_critic_torchscript.cpp
 * @brief TorchScriptCritic implementation — loads a compiled .pt model
 *        and forwards score() calls through torch::jit.
 *
 * Non-static C functions: 1 (srd_critic_create_torchscript).
 */

#include "ferrum/procgen/srd/srd_critic.h"

#include <torch/script.h>

#include <stdexcept>
#include <string>

/* ── srd_critic struct (shared with srd_critic_analytical.cpp) ──── */

/**
 * @brief Opaque wrapper holding an ISrdCritic implementation.
 *
 * Identical definition must appear in every TU that manipulates
 * srd_critic_t by value. Kept minimal: single pointer to the
 * polymorphic interface.
 */
struct srd_critic {
    ferrum::srd::ISrdCritic *impl;
};

/* ── TorchScriptCritic implementation ─────────────────────────── */

namespace ferrum::srd {

/**
 * @brief Load a TorchScript model from a .pt file.
 *
 * @param pt_path  Filesystem path to the compiled TorchScript model.
 * @throws std::runtime_error if the model cannot be loaded (wraps
 *         the underlying c10::Error message).
 */
TorchScriptCritic::TorchScriptCritic(const char *pt_path) {
    try {
        module_ = torch::jit::load(pt_path);
    } catch (const c10::Error &e) {
        throw std::runtime_error(
            std::string("TorchScriptCritic: failed to load model '") +
            pt_path + "': " + e.what());
    }
}

/**
 * @brief Forward params and types through the loaded TorchScript module.
 *
 * @param params  [N, 4] float tensor — (cx, cz, hw, hd) per box.
 * @param types   [N] int64 tensor — srd_room_type_t per box.
 * @return Scalar float tensor (lower = better).
 */
torch::Tensor TorchScriptCritic::score(torch::Tensor params,
                                       torch::Tensor types) {
    return module_.forward({params, types}).toTensor();
}

} /* namespace ferrum::srd */

/* ── C API ────────────────────────────────────────────────────── */

extern "C" {

/**
 * @brief Create a TorchScriptCritic from a compiled .pt model.
 *
 * @param pt_path  Path to the .pt file.
 * @return New critic handle, or NULL if loading fails.
 *
 * @note Ownership: caller must call srd_critic_destroy() when done.
 * @note Side effects: reads from disk; allocates heap memory.
 */
srd_critic_t *srd_critic_create_torchscript(const char *pt_path) {
    try {
        auto *impl = new ferrum::srd::TorchScriptCritic(pt_path);
        auto *c    = new srd_critic;
        c->impl    = impl;
        return c;
    } catch (...) {
        return NULL;
    }
}

} /* extern "C" */
