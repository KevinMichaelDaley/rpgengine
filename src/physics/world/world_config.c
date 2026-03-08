#include "ferrum/physics/world.h"

phys_world_config_t phys_world_config_default(void) {
    phys_world_config_t cfg = {
        .max_bodies              = 2u * 1024u * 1024u,
        .max_colliders           = 2u * 1024u * 1024u,
        .manifold_cache_size     = 4096,
        .frame_arena_size        = 32u * 1024u * 1024u, /* 32 MB */
        .fixed_dt                = 1.0f / 60.0f,      /* ~16.7 ms / 60 Hz */
        .gravity                 = {0.0f, -9.81f, 0.0f},
        .default_substeps        = 4,
        .default_solver_iterations = 16,
        .baumgarte               = 0.0f,
        .slop                    = 0.005f,
        .sleep_threshold_linear  = 0.15f,
        .sleep_threshold_angular = 0.15f,
        .sleep_delay_frames      = 120,
        .warmstart_decay         = 0.95f,
        .velocity_damping        = 0.96f,
        .max_island_bodies       = 128,
        .island_color_threshold  = 8,
        .max_joints              = 1024,
        .max_dt_override         = 3.0f,   /* Variable-dt cap: 3× fixed_dt. */
        .auto_ccd_speed          = 8.0f,   /* Auto-CCD for bodies > 8 m/s. */
        .xpbd_min_compliance     = 1e-3f,  /* Compliance floor: ρ ≈ 0.44 for
                                            * typical humanoid.  Effective
                                            * stiffness = 1000 N/m. */
    };
    return cfg;
}
