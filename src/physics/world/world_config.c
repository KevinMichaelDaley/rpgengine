#include "ferrum/physics/world.h"

phys_world_config_t phys_world_config_default(void) {
    phys_world_config_t cfg = {
        .max_bodies              = 10000,
        .max_colliders           = 10000,
        .manifold_cache_size     = 4096,
        .frame_arena_size        = 32u * 1024u * 1024u, /* 32 MB */
        .fixed_dt                = 1.0f / 60.0f,      /* ~16.7 ms / 60 Hz */
        .gravity                 = {0.0f, -9.81f, 0.0f},
        .default_substeps        = 1,
        .default_solver_iterations = 10,
        .baumgarte               = 0.0f,
        .slop                    = 0.005f,
        .sleep_threshold_linear  = 0.08f,
        .sleep_threshold_angular = 0.08f,
        .sleep_delay_frames      = 60,
        .warmstart_decay         = 0.95f,
        .velocity_damping        = 0.96f,
        .max_island_bodies       = 128,
        .island_color_threshold  = 128,
    };
    return cfg;
}
