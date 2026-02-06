CC ?= gcc
JOB_INSTRUMENTATION ?= 1
TRACY ?= 0

CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -pthread -Iinclude -Ithird_party/stb -g -O0
CFLAGS += -DFR_JOB_INSTRUMENTATION=$(JOB_INSTRUMENTATION)

LDFLAGS ?= -lm

TRACY_DIR := extern/tracy
TRACY_BUILD_DIR := $(TRACY_DIR)/build
TRACY_CLIENT_LIB := $(TRACY_BUILD_DIR)/libTracyClient.a

ifeq ($(TRACY),1)
	ifeq (,$(wildcard $(TRACY_CLIENT_LIB)))
		$(error Tracy client library missing: $(TRACY_CLIENT_LIB) (build extern/tracy first))
	endif
	CFLAGS += -DTRACY_ENABLE -I$(TRACY_DIR)/public
	LDFLAGS += -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc -Wl,--wrap=free -Wl,--wrap=aligned_alloc -Wl,--wrap=posix_memalign
	LDFLAGS += -L$(TRACY_BUILD_DIR) -lTracyClient -lstdc++ -ldl
endif
JOB_SRC := $(wildcard src/job/*.c) $(wildcard src/job/*/*.c) $(wildcard src/job/*/*/*.c)
MATH_SRC := $(wildcard src/math/*.c)
MEM_SRC_BASE := $(wildcard src/memory/*.c)
MEM_TRACY_WRAP_SRC := $(wildcard src/memory/alloc_tracy/*.c)
MEM_SRC := $(MEM_SRC_BASE)
ifeq ($(TRACY),1)
	MEM_SRC += $(MEM_TRACY_WRAP_SRC)
endif
ECS_SRC := $(wildcard src/ecs/*.c)
RENDERER_SRC := $(wildcard src/renderer/*.c) $(wildcard src/renderer/skinning/*.c)
RENDERER_DEBUG_LINES_SRC := $(wildcard src/renderer/debug_lines/*.c)
RENDERER_SRC += $(RENDERER_DEBUG_LINES_SRC)
NET_SRC := $(wildcard src/net/*.c) $(wildcard src/net/udp/*.c) $(wildcard src/net/rudp/*.c) $(wildcard src/net/rudp/reliability/*.c) $(wildcard src/net/rudp/stream/*.c) $(wildcard src/net/quantization/*.c) \
	$(wildcard src/net/replication/*.c) $(wildcard src/net/replication/*/*.c) \
	$(wildcard src/net/test/*.c) $(wildcard src/net/client/*.c) $(wildcard src/net/topic/*.c) $(wildcard src/net/topic/dispatch/*.c) \
	$(wildcard src/net/channel/*.c) $(wildcard src/net/channel/*/*.c) $(wildcard src/net/channel/*/*/*.c)
SERVER_SRC := $(wildcard src/server/repl/repl_server_*.c) $(wildcard src/server/net/fiber/*.c) $(wildcard src/server/net/runtime/*.c) \
	$(wildcard src/server/entity/*.c) $(wildcard src/server/entity/*/*.c) $(wildcard src/server/entity/*/*/*.c)
PHYS_SRC := $(wildcard src/physics/*.c) $(wildcard src/physics/*/*.c) $(wildcard src/physics/*/*/*.c)
SRC_HEADLESS := $(JOB_SRC) $(MATH_SRC) $(MEM_SRC) $(ECS_SRC) $(NET_SRC) $(SERVER_SRC) $(PHYS_SRC)
SRC_ALL := $(SRC_HEADLESS) $(RENDERER_SRC)

# Legacy prerequisite variable used by some build rules.
SRC := $(SRC_HEADLESS)

SDL2_CFLAGS := $(shell sdl2-config --cflags 2>/dev/null)
SDL2_LIBS := $(shell sdl2-config --libs 2>/dev/null)
GLEW_LIBS := $(shell pkg-config --libs glew 2>/dev/null)
GL_LIBS := -lGL
RENDERER_TEST_CFLAGS := $(SDL2_CFLAGS)
RENDERER_TEST_LIBS := $(SDL2_LIBS) $(GLEW_LIBS) -lSDL2 -lGLEW $(GL_LIBS)

BIN_HEADLESS := build/p000_tests build/p001_tests build/p002_tests build/p003_tests \
	build/p007_net_tests build/p007_net_header_tests build/p007_net_ack_tests build/p007_net_unreliable_tests \
	build/p007_net_reliable_tests build/p007_net_schema_registry_tests \
	build/p007_net_rudp_fragmentation_tests \
	build/p012_net_rudp_reliability_boundary_tests \
	build/p013_net_rudp_reliability_layer_tests \
	build/p014_net_rudp_reliability_send_layer_tests \
	build/p015_server_net_inbound_message_tests \
	build/p016_net_repl_input_rot_tests \
	build/p017_math_quat_angle_tests \
	build/p018_physics_types_tests \
	build/p019_physics_body_tests \
	build/p020_physics_collider_tests \
	build/p021_physics_aabb_tests \
	build/p022_physics_pool_arena_tests \
	build/p023_physics_manifold_tests \
	build/p024_physics_constraint_tests \
	build/p025_physics_game_state_tests \
	build/p026_physics_compound_collider_tests \
	build/p027_physics_tier_list_tests \
	build/p028_physics_spatial_grid_tests \
	build/p029_physics_manifold_cache_tests \
	build/p030_physics_island_tests \
	build/p031_physics_world_tests \
	build/p032_physics_phase0_integration_tests \
	build/p033_physics_step_plan_tests \
	build/p034_physics_tier_classify_tests \
	build/p035_physics_spatial_update_tests \
	build/p036_physics_halo_closure_tests \
	build/p037_physics_aabb_update_tests \
	build/p038_physics_broadphase_tests \
	build/p039_physics_narrowphase_tests \
	build/p040_physics_manifold_build_tests \
	build/p041_physics_stabilization_tests \
	build/p042_physics_constraint_build_tests \
	build/p043_physics_island_build_tests \
	build/p044_physics_tgs_solve_tests \
	build/p045_physics_xpbd_solve_tests \
	build/p046_physics_solver_transition_tests \
	build/p047_physics_integrate_tests \
	build/p048_physics_cache_commit_tests \
	build/p049_physics_tick_tests \
	build/p050_physics_impact_event_tests \
	build/p051_physics_snapshot_tests \
	build/p052_physics_prediction_tests \
	build/p053_physics_phase1_integration_tests \
	build/p054_physics_sphere_box_tests \
	build/p055_physics_sphere_capsule_tests \
	build/p056_physics_box_box_tests \
	build/p057_physics_box_capsule_tests \
	build/p058_physics_capsule_capsule_tests \
	build/p059_physics_phase2_integration_tests \
	build/p060_physics_job_infra_tests \
	build/p061_physics_par_tier_tests \
	build/p062_physics_par_spatial_tests \
	build/p063_physics_par_broadphase_tests \
	build/p064_physics_par_narrowphase_tests \
	build/p065_physics_par_manifold_tests \
	build/p066_physics_par_stabilization_tests \
	build/p067_physics_par_constraint_tests \
	build/p068_physics_par_tgs_tests \
	build/p069_physics_par_xpbd_tests \
	build/p070_physics_par_integrate_tests \
	build/p071_physics_par_tick_tests \
	build/p072_physics_phase3_integration_tests \
	build/p073_physics_tier_distance_tests \
	build/p074_physics_tier_params_tests \
	build/p075_physics_solver_transition_tests \
	build/p076_physics_tier_stabilization_tests \
	build/p077_physics_amortized_t4_tests \
	build/p079_physics_sphere_simplify_tests \
	build/p007_net_udp_socket_tests build/p007_net_integration_server_tests build/p007_net_integration_client_tests \
	build/p008_net_repl_server build/p008_net_repl_client build/p008_net_multi_client_server_integration_tests \
	build/p008_net_perf_server_tests build/p008_net_perf_client_tests \
	build/p000_job_performance_tests build/p002_memory_apool_tests build/p007_net_topic_dispatch_tests build/p007_net_topic_dispatch_benchmark \
	build/p008_server_compute_jobs_tests build/p007_net_stream_api_tests build/p007_net_stream_channel_topic_tests \
	build/p008_server_client_fiber_stream_tests build/p008_server_net_runtime_fiber_tests \
	build/p008_server_entity_net_pump_tests \
	build/p008_pose_interpolator_tests \
	build/p009_server_state_update_queue_tests \
	build/p009_net_topic_channel_ring_tests \
	build/p011_renderer_correction_debug_lines_tests \
	build/p000_job_queue_diagnostics_tests \
	build/p000_ws_deque_tests

ifeq ($(TRACY),1)
BIN_HEADLESS += build/p010_tracy_alloc_override_tests
endif

BIN_RENDERER_TESTS := build/p004_tests build/p004_shader_tests build/p004_buffer_tests \
	build/p004_uniform_tests build/p004_palette_tests build/p004_pipeline_tests \
	build/p004_skinning_tests build/p004_ecs_skinning_tests build/p004_skinning_alloc_tests \
	build/p004_pipeline_resource_tests build/p004_pipeline_graph_tests

BIN := $(BIN_HEADLESS) $(BIN_RENDERER_TESTS)

.PHONY: all test test_renderer clean p008_build p008_test p008_help p008_perf p008_renderer_client

all: $(BIN)

build/p000_tests: $(JOB_SRC) $(MEM_SRC) tests/p000_fiber_job_system_tests.c | build
	$(CC) $(CFLAGS) tests/p000_fiber_job_system_tests.c $(JOB_SRC) $(MEM_SRC) -o $@ $(LDFLAGS)

## AddressSanitizer does not support custom fiber stacks without special hooks.
## Build the perf harness without ASan to avoid false-positive crashes.
CFLAGS_NO_ASAN := $(filter-out -fsanitize=address,$(CFLAGS))
build/p000_job_performance_tests: $(SRC) tests/p000_job_performance_tests.c | build
	$(CC) $(CFLAGS_NO_ASAN) tests/p000_job_performance_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p001_tests: $(SRC) tests/p001_core_math_tests.c | build
	$(CC) $(CFLAGS) tests/p001_core_math_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p002_tests: $(SRC) tests/p002_memory_tests.c | build
	$(CC) $(CFLAGS) tests/p002_memory_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p002_memory_apool_tests: $(SRC) tests/p002_memory_apool_tests.c | build
	$(CC) $(CFLAGS) tests/p002_memory_apool_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p003_tests: $(SRC) tests/p003_ecs_tests.c | build
	$(CC) $(CFLAGS) tests/p003_ecs_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p000_job_queue_sharding_tests: $(SRC) tests/p000_job_queue_sharding_tests.c | build
	$(CC) $(CFLAGS) tests/p000_job_queue_sharding_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p000_job_queue_diagnostics_tests: $(SRC) tests/p000_job_queue_diagnostics_tests.c | build
	$(CC) $(CFLAGS) tests/p000_job_queue_diagnostics_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p000_ws_deque_tests: $(SRC) tests/p000_ws_deque_tests.c | build
	$(CC) $(CFLAGS) tests/p000_ws_deque_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p007_net_tests: $(SRC) tests/p007_net_test_utils_tests.c | build
	$(CC) $(CFLAGS) tests/p007_net_test_utils_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p007_net_header_tests: $(SRC) tests/p007_net_header_tests.c | build
	$(CC) $(CFLAGS) tests/p007_net_header_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p007_net_ack_tests: $(SRC) tests/p007_net_ack_tests.c | build
	$(CC) $(CFLAGS) tests/p007_net_ack_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p007_net_unreliable_tests: $(SRC) tests/p007_net_unreliable_tests.c | build
	$(CC) $(CFLAGS) tests/p007_net_unreliable_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p007_net_schema_registry_tests: $(SRC) tests/p007_net_schema_registry_tests.c | build
	$(CC) $(CFLAGS) tests/p007_net_schema_registry_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p007_net_reliable_tests: $(SRC) tests/p007_net_reliable_tests.c | build
	$(CC) $(CFLAGS) tests/p007_net_reliable_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p007_net_rudp_fragmentation_tests: $(SRC) tests/net_rudp_fragmentation_tests.c | build
	$(CC) $(CFLAGS) tests/net_rudp_fragmentation_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p012_net_rudp_reliability_boundary_tests: $(SRC) tests/p012_net_rudp_reliability_boundary_tests.c | build
	$(CC) $(CFLAGS) tests/p012_net_rudp_reliability_boundary_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p013_net_rudp_reliability_layer_tests: $(SRC) tests/p013_net_rudp_reliability_layer_tests.c | build
	$(CC) $(CFLAGS) tests/p013_net_rudp_reliability_layer_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p014_net_rudp_reliability_send_layer_tests: $(SRC) tests/p014_net_rudp_reliability_send_layer_tests.c | build
	$(CC) $(CFLAGS) tests/p014_net_rudp_reliability_send_layer_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p015_server_net_inbound_message_tests: $(SRC) tests/p015_server_net_inbound_message_tests.c | build
	$(CC) $(CFLAGS) tests/p015_server_net_inbound_message_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p016_net_repl_input_rot_tests: $(SRC) tests/p016_net_repl_input_rot_tests.c | build
	$(CC) $(CFLAGS) tests/p016_net_repl_input_rot_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p017_math_quat_angle_tests: $(SRC) tests/p017_math_quat_angle_tests.c | build
	$(CC) $(CFLAGS) tests/p017_math_quat_angle_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p018_physics_types_tests: $(SRC) tests/p018_physics_types_tests.c | build
	$(CC) $(CFLAGS) tests/p018_physics_types_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p019_physics_body_tests: $(SRC) tests/p019_physics_body_tests.c | build
	$(CC) $(CFLAGS) tests/p019_physics_body_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p020_physics_collider_tests: $(SRC) tests/p020_physics_collider_tests.c | build
	$(CC) $(CFLAGS) tests/p020_physics_collider_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p021_physics_aabb_tests: $(SRC) tests/p021_physics_aabb_tests.c | build
	$(CC) $(CFLAGS) tests/p021_physics_aabb_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p022_physics_pool_arena_tests: $(SRC) tests/p022_physics_pool_arena_tests.c | build
	$(CC) $(CFLAGS) tests/p022_physics_pool_arena_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p023_physics_manifold_tests: $(SRC) tests/p023_physics_manifold_tests.c | build
	$(CC) $(CFLAGS) tests/p023_physics_manifold_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p024_physics_constraint_tests: $(SRC) tests/p024_physics_constraint_tests.c | build
	$(CC) $(CFLAGS) tests/p024_physics_constraint_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p025_physics_game_state_tests: $(SRC) tests/p025_physics_game_state_tests.c | build
	$(CC) $(CFLAGS) tests/p025_physics_game_state_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p026_physics_compound_collider_tests: $(SRC) tests/p026_physics_compound_collider_tests.c | build
	$(CC) $(CFLAGS) tests/p026_physics_compound_collider_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p027_physics_tier_list_tests: $(SRC) tests/p027_physics_tier_list_tests.c | build
	$(CC) $(CFLAGS) tests/p027_physics_tier_list_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p028_physics_spatial_grid_tests: $(SRC) tests/p028_physics_spatial_grid_tests.c | build
	$(CC) $(CFLAGS) tests/p028_physics_spatial_grid_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p029_physics_manifold_cache_tests: $(SRC) tests/p029_physics_manifold_cache_tests.c | build
	$(CC) $(CFLAGS) tests/p029_physics_manifold_cache_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p030_physics_island_tests: $(SRC) tests/p030_physics_island_tests.c | build
	$(CC) $(CFLAGS) tests/p030_physics_island_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p031_physics_world_tests: $(SRC) tests/p031_physics_world_tests.c | build
	$(CC) $(CFLAGS) tests/p031_physics_world_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p032_physics_phase0_integration_tests: $(SRC) tests/p032_physics_phase0_integration_tests.c | build
	$(CC) $(CFLAGS) tests/p032_physics_phase0_integration_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p033_physics_step_plan_tests: $(SRC) tests/p033_physics_step_plan_tests.c | build
	$(CC) $(CFLAGS) tests/p033_physics_step_plan_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p034_physics_tier_classify_tests: $(SRC) tests/p034_physics_tier_classify_tests.c | build
	$(CC) $(CFLAGS) tests/p034_physics_tier_classify_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p035_physics_spatial_update_tests: $(SRC) tests/p035_physics_spatial_update_tests.c | build
	$(CC) $(CFLAGS) tests/p035_physics_spatial_update_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p036_physics_halo_closure_tests: $(SRC) tests/p036_physics_halo_closure_tests.c | build
	$(CC) $(CFLAGS) tests/p036_physics_halo_closure_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p037_physics_aabb_update_tests: $(SRC) tests/p037_physics_aabb_update_tests.c | build
	$(CC) $(CFLAGS) tests/p037_physics_aabb_update_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p038_physics_broadphase_tests: $(SRC) tests/p038_physics_broadphase_tests.c | build
	$(CC) $(CFLAGS) tests/p038_physics_broadphase_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p039_physics_narrowphase_tests: $(SRC) tests/p039_physics_narrowphase_tests.c | build
	$(CC) $(CFLAGS) tests/p039_physics_narrowphase_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p040_physics_manifold_build_tests: $(SRC) tests/p040_physics_manifold_build_tests.c | build
	$(CC) $(CFLAGS) tests/p040_physics_manifold_build_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p041_physics_stabilization_tests: $(SRC) tests/p041_physics_stabilization_tests.c | build
	$(CC) $(CFLAGS) tests/p041_physics_stabilization_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p042_physics_constraint_build_tests: $(SRC) tests/p042_physics_constraint_build_tests.c | build
	$(CC) $(CFLAGS) tests/p042_physics_constraint_build_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p043_physics_island_build_tests: $(SRC) tests/p043_physics_island_build_tests.c | build
	$(CC) $(CFLAGS) tests/p043_physics_island_build_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p044_physics_tgs_solve_tests: $(SRC) tests/p044_physics_tgs_solve_tests.c | build
	$(CC) $(CFLAGS) tests/p044_physics_tgs_solve_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p045_physics_xpbd_solve_tests: $(SRC) tests/p045_physics_xpbd_solve_tests.c | build
	$(CC) $(CFLAGS) tests/p045_physics_xpbd_solve_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p046_physics_solver_transition_tests: $(SRC) tests/p046_physics_solver_transition_tests.c | build
	$(CC) $(CFLAGS) tests/p046_physics_solver_transition_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p047_physics_integrate_tests: $(SRC) tests/p047_physics_integrate_tests.c | build
	$(CC) $(CFLAGS) tests/p047_physics_integrate_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p048_physics_cache_commit_tests: $(SRC) tests/p048_physics_cache_commit_tests.c | build
	$(CC) $(CFLAGS) tests/p048_physics_cache_commit_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p049_physics_tick_tests: $(SRC) tests/p049_physics_tick_tests.c | build
	$(CC) $(CFLAGS) tests/p049_physics_tick_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p050_physics_impact_event_tests: $(SRC) tests/p050_physics_impact_event_tests.c | build
	$(CC) $(CFLAGS) tests/p050_physics_impact_event_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p051_physics_snapshot_tests: $(SRC) tests/p051_physics_snapshot_tests.c | build
	$(CC) $(CFLAGS) tests/p051_physics_snapshot_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p052_physics_prediction_tests: $(SRC) tests/p052_physics_prediction_tests.c | build
	$(CC) $(CFLAGS) tests/p052_physics_prediction_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p053_physics_phase1_integration_tests: $(SRC) tests/p053_physics_phase1_integration_tests.c | build
	$(CC) $(CFLAGS) tests/p053_physics_phase1_integration_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p054_physics_sphere_box_tests: $(SRC) tests/p054_physics_sphere_box_tests.c | build
	$(CC) $(CFLAGS) tests/p054_physics_sphere_box_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p055_physics_sphere_capsule_tests: $(SRC) tests/p055_physics_sphere_capsule_tests.c | build
	$(CC) $(CFLAGS) tests/p055_physics_sphere_capsule_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p056_physics_box_box_tests: $(SRC) tests/p056_physics_box_box_tests.c | build
	$(CC) $(CFLAGS) tests/p056_physics_box_box_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p057_physics_box_capsule_tests: $(SRC) tests/p057_physics_box_capsule_tests.c | build
	$(CC) $(CFLAGS) tests/p057_physics_box_capsule_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p058_physics_capsule_capsule_tests: $(SRC) tests/p058_physics_capsule_capsule_tests.c | build
	$(CC) $(CFLAGS) tests/p058_physics_capsule_capsule_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p059_physics_phase2_integration_tests: $(SRC) tests/p059_physics_phase2_integration_tests.c | build
	$(CC) $(CFLAGS) tests/p059_physics_phase2_integration_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p060_physics_job_infra_tests: $(SRC) tests/p060_physics_job_infra_tests.c | build
	$(CC) $(CFLAGS) tests/p060_physics_job_infra_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p061_physics_par_tier_tests: $(SRC) tests/p061_physics_par_tier_tests.c | build
	$(CC) $(CFLAGS) tests/p061_physics_par_tier_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p062_physics_par_spatial_tests: $(SRC) tests/p062_physics_par_spatial_tests.c | build
	$(CC) $(CFLAGS) tests/p062_physics_par_spatial_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p063_physics_par_broadphase_tests: $(SRC) tests/p063_physics_par_broadphase_tests.c | build
	$(CC) $(CFLAGS) tests/p063_physics_par_broadphase_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p064_physics_par_narrowphase_tests: $(SRC) tests/p064_physics_par_narrowphase_tests.c | build
	$(CC) $(CFLAGS) tests/p064_physics_par_narrowphase_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p065_physics_par_manifold_tests: $(SRC) tests/p065_physics_par_manifold_tests.c | build
	$(CC) $(CFLAGS) tests/p065_physics_par_manifold_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p066_physics_par_stabilization_tests: $(SRC) tests/p066_physics_par_stabilization_tests.c | build
	$(CC) $(CFLAGS) tests/p066_physics_par_stabilization_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p067_physics_par_constraint_tests: $(SRC) tests/p067_physics_par_constraint_tests.c | build
	$(CC) $(CFLAGS) tests/p067_physics_par_constraint_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p068_physics_par_tgs_tests: $(SRC) tests/p068_physics_par_tgs_tests.c | build
	$(CC) $(CFLAGS) tests/p068_physics_par_tgs_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p069_physics_par_xpbd_tests: $(SRC) tests/p069_physics_par_xpbd_tests.c | build
	$(CC) $(CFLAGS) tests/p069_physics_par_xpbd_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p070_physics_par_integrate_tests: $(SRC) tests/p070_physics_par_integrate_tests.c | build
	$(CC) $(CFLAGS) tests/p070_physics_par_integrate_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p071_physics_par_tick_tests: $(SRC) tests/p071_physics_par_tick_tests.c | build
	$(CC) $(CFLAGS) tests/p071_physics_par_tick_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p072_physics_phase3_integration_tests: $(SRC) tests/p072_physics_phase3_integration_tests.c | build
	$(CC) $(CFLAGS) tests/p072_physics_phase3_integration_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p073_physics_tier_distance_tests: $(SRC) tests/p073_physics_tier_distance_tests.c | build
	$(CC) $(CFLAGS) tests/p073_physics_tier_distance_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p074_physics_tier_params_tests: $(SRC) tests/p074_physics_tier_params_tests.c | build
	$(CC) $(CFLAGS) tests/p074_physics_tier_params_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p075_physics_solver_transition_tests: $(SRC) tests/p075_physics_solver_transition_tests.c | build
	$(CC) $(CFLAGS) tests/p075_physics_solver_transition_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p076_physics_tier_stabilization_tests: $(SRC) tests/p076_physics_tier_stabilization_tests.c | build
	$(CC) $(CFLAGS) tests/p076_physics_tier_stabilization_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p077_physics_amortized_t4_tests: $(SRC) tests/p077_physics_amortized_t4_tests.c | build
	$(CC) $(CFLAGS) tests/p077_physics_amortized_t4_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p079_physics_sphere_simplify_tests: $(SRC) tests/p079_physics_sphere_simplify_tests.c | build
	$(CC) $(CFLAGS) tests/p079_physics_sphere_simplify_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p007_net_udp_socket_tests: $(SRC) tests/p007_net_udp_socket_tests.c | build
	$(CC) $(CFLAGS) tests/p007_net_udp_socket_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p007_net_integration_server_tests: $(SRC) tests/p007_net_integration_server_tests.c | build
	$(CC) $(CFLAGS) tests/p007_net_integration_server_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p007_net_integration_client_tests: $(SRC) tests/p007_net_integration_client_tests.c | build
	$(CC) $(CFLAGS) tests/p007_net_integration_client_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p007_net_client_rx_tests: $(SRC) tests/p007_net_client_rx_tests.c | build
	$(CC) $(CFLAGS) tests/p007_net_client_rx_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p007_net_client_rx_udp_topic_tests: $(SRC) tests/p007_net_client_rx_udp_topic_tests.c | build
	$(CC) $(CFLAGS) tests/p007_net_client_rx_udp_topic_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p007_net_topic_dispatch_tests: $(SRC) tests/p007_net_topic_dispatch_tests.c | build
	$(CC) $(CFLAGS) tests/p007_net_topic_dispatch_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p008_pose_interpolator_tests: $(SRC) tests/p008_pose_interpolator_tests.c | build
	$(CC) $(CFLAGS) tests/p008_pose_interpolator_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p007_net_topic_dispatch_benchmark: $(SRC) tests/p007_net_topic_dispatch_benchmark.c | build
	$(CC) $(CFLAGS) tests/p007_net_topic_dispatch_benchmark.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p007_net_stream_api_tests: $(SRC) tests/p007_net_stream_api_tests.c | build
	$(CC) $(CFLAGS) tests/p007_net_stream_api_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p007_net_stream_channel_topic_tests: $(SRC) tests/p007_net_stream_channel_topic_tests.c | build
	$(CC) $(CFLAGS) tests/p007_net_stream_channel_topic_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p007_net_stream_perf_benchmark: $(SRC) tests/p007_net_stream_perf_benchmark.c | build
	$(CC) $(CFLAGS) tests/p007_net_stream_perf_benchmark.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p008_server_client_fiber_stream_tests: $(SRC) tests/p008_server_client_fiber_stream_tests.c | build
	$(CC) $(CFLAGS) tests/p008_server_client_fiber_stream_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p008_server_net_runtime_fiber_tests: $(SRC) tests/p008_server_net_runtime_fiber_tests.c | build
	$(CC) $(CFLAGS) tests/p008_server_net_runtime_fiber_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p008_server_entity_net_pump_tests: $(SRC) tests/p008_server_entity_net_pump_tests.c | build
	$(CC) $(CFLAGS) tests/p008_server_entity_net_pump_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p008_net_repl_server: $(SRC) tests/p008_net_repl_server.c | build
	$(CC) $(CFLAGS) tests/p008_net_repl_server.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p008_net_repl_client: $(SRC) tests/p008_net_repl_client.c | build
	$(CC) $(CFLAGS) tests/p008_net_repl_client.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p008_net_multi_client_server_integration_tests: $(SRC) tests/p008_net_multi_client_server_integration_tests.c | build
	$(CC) $(CFLAGS) tests/p008_net_multi_client_server_integration_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p008_net_perf_server_tests: $(SRC) tests/p008_net_perf_server_tests.c | build
	$(CC) $(CFLAGS) tests/p008_net_perf_server_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p008_net_perf_client_tests: $(SRC) tests/p008_net_perf_client_tests.c | build
	$(CC) $(CFLAGS) tests/p008_net_perf_client_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p008_renderer_client: $(SRC) tests/p008_renderer_client.c | build
	$(CC) $(CFLAGS) $(RENDERER_TEST_CFLAGS) tests/p008_renderer_client.c $(SRC_ALL) -o $@ $(LDFLAGS) $(RENDERER_TEST_LIBS)

build/p008_server_compute_jobs_tests: $(SRC) tests/p008_server_compute_jobs_tests.c | build
	$(CC) $(CFLAGS) tests/p008_server_compute_jobs_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p009_server_state_update_queue_tests: $(SRC) tests/p009_server_state_update_queue_tests.c | build
	$(CC) $(CFLAGS) tests/p009_server_state_update_queue_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p009_net_topic_channel_ring_tests: $(SRC) tests/p009_net_topic_channel_ring_tests.c | build
	$(CC) $(CFLAGS) tests/p009_net_topic_channel_ring_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p010_tracy_alloc_override_tests: $(SRC) tests/p010_tracy_alloc_override_tests.c | build
	$(CC) $(CFLAGS) tests/p010_tracy_alloc_override_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p011_renderer_correction_debug_lines_tests: $(SRC) $(RENDERER_DEBUG_LINES_SRC) tests/p011_renderer_correction_debug_lines_tests.c | build
	$(CC) $(CFLAGS) tests/p011_renderer_correction_debug_lines_tests.c $(SRC_HEADLESS) $(RENDERER_DEBUG_LINES_SRC) -o $@ $(LDFLAGS)

# RED tests (may not compile until quantization module exists)
build/p007_net_quantization_determinism_tests: $(SRC) tests/p007_net_quantization_determinism_tests.c | build
	$(CC) $(CFLAGS) tests/p007_net_quantization_determinism_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)
.PHONY: test_red

# RED tests (may not compile until replication protocol exists)
build/p008_net_replication_protocol_tests: $(SRC) tests/p008_net_replication_protocol_tests.c | build
	$(CC) $(CFLAGS) tests/p008_net_replication_protocol_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)
.PHONY: test_red_p008

# Note: this test currently depends on reliable ordered channel implementation.
build/p007_net_reliable_ordered_tests: $(SRC) tests/p007_net_reliable_ordered_tests.c | build
	$(CC) $(CFLAGS) tests/p007_net_reliable_ordered_tests.c $(SRC_HEADLESS) -o $@ $(LDFLAGS)

build/p004_tests: $(SRC) tests/p004_renderer_gl_loader_tests.c | build
	$(CC) $(CFLAGS) tests/p004_renderer_gl_loader_tests.c $(SRC_ALL) -o $@ $(LDFLAGS)

build/p004_shader_tests: $(SRC) tests/p004_renderer_shader_tests.c | build
	$(CC) $(CFLAGS) $(RENDERER_TEST_CFLAGS) tests/p004_renderer_shader_tests.c \
$(SRC_ALL) -o $@ $(LDFLAGS) $(RENDERER_TEST_LIBS)

build/p004_buffer_tests: $(SRC) tests/p004_renderer_buffer_tests.c | build
	$(CC) $(CFLAGS) $(RENDERER_TEST_CFLAGS) tests/p004_renderer_buffer_tests.c \
$(SRC_ALL) -o $@ $(LDFLAGS) $(RENDERER_TEST_LIBS)

build/p004_uniform_tests: $(SRC) tests/p004_renderer_uniform_tests.c | build
	$(CC) $(CFLAGS) $(RENDERER_TEST_CFLAGS) tests/p004_renderer_uniform_tests.c \
$(SRC_ALL) -o $@ $(LDFLAGS) $(RENDERER_TEST_LIBS)

build/p004_palette_tests: $(SRC) tests/p004_renderer_palette_tests.c | build
	$(CC) $(CFLAGS) $(RENDERER_TEST_CFLAGS) tests/p004_renderer_palette_tests.c \
$(SRC_ALL) -o $@ $(LDFLAGS) $(RENDERER_TEST_LIBS)

build/p004_pipeline_tests: $(SRC) tests/p004_renderer_pipeline_tests.c | build
	$(CC) $(CFLAGS) tests/p004_renderer_pipeline_tests.c $(SRC_ALL) -o $@ $(LDFLAGS)

build/p004_skinning_tests: $(SRC) tests/p004_renderer_skinning_tests.c | build
	$(CC) $(CFLAGS) $(RENDERER_TEST_CFLAGS) tests/p004_renderer_skinning_tests.c \
$(SRC_ALL) -o $@ $(LDFLAGS) $(RENDERER_TEST_LIBS)

build/p004_ecs_skinning_tests: $(SRC) tests/p004_renderer_ecs_skinning_tests.c | build
	$(CC) $(CFLAGS) $(RENDERER_TEST_CFLAGS) tests/p004_renderer_ecs_skinning_tests.c \
$(SRC_ALL) -o $@ $(LDFLAGS) $(RENDERER_TEST_LIBS)

build/p004_skinning_alloc_tests: $(SRC) tests/p004_renderer_skinning_alloc_tests.c | build
	$(CC) $(CFLAGS) tests/p004_renderer_skinning_alloc_tests.c $(SRC_ALL) -o $@ $(LDFLAGS)

build/p004_pipeline_resource_tests: $(SRC) tests/p004_renderer_pipeline_resource_tests.c | build
	$(CC) $(CFLAGS) $(RENDERER_TEST_CFLAGS) tests/p004_renderer_pipeline_resource_tests.c \
$(SRC_ALL) -o $@ $(LDFLAGS) $(RENDERER_TEST_LIBS)

build/p004_pipeline_graph_tests: $(SRC) tests/p004_renderer_pipeline_graph_tests.c | build
	$(CC) $(CFLAGS) $(RENDERER_TEST_CFLAGS) tests/p004_renderer_pipeline_graph_tests.c \
$(SRC_ALL) -o $@ $(LDFLAGS) $(RENDERER_TEST_LIBS)
build:
	@mkdir -p build


test: $(BIN_HEADLESS) build/p000_job_queue_sharding_tests build/p000_job_queue_diagnostics_tests build/p000_ws_deque_tests build/p007_net_client_rx_tests build/p007_net_client_rx_udp_topic_tests build/p007_net_topic_dispatch_tests
	./build/p000_tests && ./build/p001_tests && ./build/p002_tests && ./build/p002_memory_apool_tests && ./build/p003_tests \
&& ./build/p007_net_tests && ./build/p007_net_header_tests && ./build/p007_net_ack_tests \
&& ./build/p007_net_unreliable_tests && ./build/p007_net_reliable_tests && ./build/p007_net_rudp_fragmentation_tests \
	&& ./build/p012_net_rudp_reliability_boundary_tests \
	&& ./build/p013_net_rudp_reliability_layer_tests \
	&& ./build/p014_net_rudp_reliability_send_layer_tests \
	&& ./build/p015_server_net_inbound_message_tests \
	&& ./build/p016_net_repl_input_rot_tests \
	&& ./build/p017_math_quat_angle_tests \
	&& ./build/p018_physics_types_tests \
	&& ./build/p019_physics_body_tests \
	&& ./build/p020_physics_collider_tests \
	&& ./build/p021_physics_aabb_tests \
	&& ./build/p022_physics_pool_arena_tests \
	&& ./build/p023_physics_manifold_tests \
	&& ./build/p024_physics_constraint_tests \
	&& ./build/p025_physics_game_state_tests \
	&& ./build/p026_physics_compound_collider_tests \
	&& ./build/p027_physics_tier_list_tests \
	&& ./build/p028_physics_spatial_grid_tests \
	&& ./build/p029_physics_manifold_cache_tests \
	&& ./build/p030_physics_island_tests \
	&& ./build/p031_physics_world_tests \
	&& ./build/p032_physics_phase0_integration_tests \
	&& ./build/p033_physics_step_plan_tests \
	&& ./build/p034_physics_tier_classify_tests \
	&& ./build/p035_physics_spatial_update_tests \
	&& ./build/p036_physics_halo_closure_tests \
	&& ./build/p037_physics_aabb_update_tests \
	&& ./build/p038_physics_broadphase_tests \
	&& ./build/p039_physics_narrowphase_tests \
	&& ./build/p040_physics_manifold_build_tests \
	&& ./build/p041_physics_stabilization_tests \
	&& ./build/p042_physics_constraint_build_tests \
	&& ./build/p043_physics_island_build_tests \
	&& ./build/p044_physics_tgs_solve_tests \
	&& ./build/p045_physics_xpbd_solve_tests \
	&& ./build/p046_physics_solver_transition_tests \
	&& ./build/p047_physics_integrate_tests \
	&& ./build/p048_physics_cache_commit_tests \
	&& ./build/p049_physics_tick_tests \
	&& ./build/p050_physics_impact_event_tests \
	&& ./build/p051_physics_snapshot_tests \
	&& ./build/p052_physics_prediction_tests \
	&& ./build/p053_physics_phase1_integration_tests \
	&& ./build/p054_physics_sphere_box_tests \
	&& ./build/p055_physics_sphere_capsule_tests \
	&& ./build/p056_physics_box_box_tests \
	&& ./build/p057_physics_box_capsule_tests \
	&& ./build/p058_physics_capsule_capsule_tests \
	&& ./build/p059_physics_phase2_integration_tests \
	&& ./build/p060_physics_job_infra_tests \
	&& ./build/p061_physics_par_tier_tests \
	&& ./build/p062_physics_par_spatial_tests \
	&& ./build/p063_physics_par_broadphase_tests \
	&& ./build/p064_physics_par_narrowphase_tests \
	&& ./build/p065_physics_par_manifold_tests \
	&& ./build/p066_physics_par_stabilization_tests \
	&& ./build/p067_physics_par_constraint_tests \
	&& ./build/p068_physics_par_tgs_tests \
	&& ./build/p069_physics_par_xpbd_tests \
	&& ./build/p070_physics_par_integrate_tests \
	&& ./build/p071_physics_par_tick_tests \
	&& ./build/p072_physics_phase3_integration_tests \
	&& ./build/p073_physics_tier_distance_tests \
	&& ./build/p074_physics_tier_params_tests \
	&& ./build/p075_physics_solver_transition_tests \
	&& ./build/p076_physics_tier_stabilization_tests \
	&& ./build/p077_physics_amortized_t4_tests \
	&& ./build/p079_physics_sphere_simplify_tests \
&& ./build/p007_net_schema_registry_tests \
	&& ./build/p007_net_udp_socket_tests \
	&& ./build/p008_pose_interpolator_tests \
	&& ./build/p009_server_state_update_queue_tests \
	&& ./build/p000_job_queue_diagnostics_tests \
	&& ./build/p000_ws_deque_tests \
	&& ./build/p007_net_client_rx_tests \
	&& ./build/p007_net_client_rx_udp_topic_tests \
	&& ./build/p007_net_topic_dispatch_tests \
	&& ./build/p011_renderer_correction_debug_lines_tests \
	&& ( [ "$(TRACY)" != "1" ] || ./build/p010_tracy_alloc_override_tests )

TEST_TIMEOUT ?= 20

.PHONY: test_timeout
test_timeout: $(BIN_HEADLESS) build/p000_job_queue_sharding_tests build/p000_job_queue_diagnostics_tests build/p000_ws_deque_tests build/p007_net_client_rx_tests build/p007_net_client_rx_udp_topic_tests build/p007_net_topic_dispatch_tests
	@set -e; \
	for t in \
		./build/p000_tests \
		./build/p001_tests \
		./build/p002_tests \
		./build/p002_memory_apool_tests \
		./build/p003_tests \
		./build/p007_net_tests \
		./build/p007_net_header_tests \
		./build/p007_net_ack_tests \
		./build/p007_net_unreliable_tests \
		./build/p007_net_reliable_tests \
		./build/p007_net_rudp_fragmentation_tests \
		./build/p012_net_rudp_reliability_boundary_tests \
		./build/p013_net_rudp_reliability_layer_tests \
		./build/p014_net_rudp_reliability_send_layer_tests \
		./build/p015_server_net_inbound_message_tests \
		./build/p007_net_schema_registry_tests \
		./build/p007_net_udp_socket_tests \
		./build/p008_pose_interpolator_tests \
		./build/p009_server_state_update_queue_tests \
		./build/p000_job_queue_diagnostics_tests \
		./build/p000_ws_deque_tests \
		./build/p007_net_client_rx_tests \
		./build/p007_net_client_rx_udp_topic_tests \
		./build/p007_net_topic_dispatch_tests \
		./build/p011_renderer_correction_debug_lines_tests \
	; do \
		echo "RUN $$t"; \
		timeout --foreground $(TEST_TIMEOUT)s $$t; \
	done; \
	if [ "$(TRACY)" = "1" ]; then \
		echo "RUN ./build/p010_tracy_alloc_override_tests"; \
		timeout --foreground $(TEST_TIMEOUT)s ./build/p010_tracy_alloc_override_tests; \
	fi

.PHONY: perf_job
perf_job: build/p000_job_performance_tests
	./build/p000_job_performance_tests

.PHONY: repro_p000_hang
REPRO_ITERS ?= 100
REPRO_TIMEOUT ?= 2
repro_p000_hang: build/p000_tests
	ITERS=$(REPRO_ITERS) TIMEOUT_SECS=$(REPRO_TIMEOUT) ./scripts/repro_p000_hang.sh ./build/p000_tests

test_renderer: $(BIN_RENDERER_TESTS)
	./build/p004_tests && ./build/p004_shader_tests && ./build/p004_buffer_tests \
	&& ./build/p004_uniform_tests && ./build/p004_palette_tests && ./build/p004_pipeline_tests \
	&& ./build/p004_skinning_tests && ./build/p004_ecs_skinning_tests \
	&& ./build/p004_skinning_alloc_tests && ./build/p004_pipeline_resource_tests \
	&& ./build/p004_pipeline_graph_tests

test_red: build/p007_net_quantization_determinism_tests
	./build/p007_net_quantization_determinism_tests

test_red_p008: build/p008_net_replication_protocol_tests
	./build/p008_net_replication_protocol_tests

.PHONY: test_p008

test_p008: build/p008_net_multi_client_server_integration_tests build/p008_net_repl_server build/p008_net_repl_client
	./build/p008_net_multi_client_server_integration_tests

p008_build: build/p008_net_repl_server build/p008_net_repl_client build/p008_net_multi_client_server_integration_tests build/p008_net_perf_server_tests build/p008_net_perf_client_tests

p008_test: test_p008

p008_help:
	@echo "P_008 targets:";
	@echo "  make p008_build   # build headless p008 binaries";
	@echo "  make test_p008    # run multi-process integration test";
	@echo "  make p008_perf    # perf harness entrypoint";
	@echo "  make p008_renderer_client  # renderer client entrypoint (when implemented)";
	@echo "See: tests/p008_net_integration_README.md"

p008_perf:
	@bash -euo pipefail -c ' \
		port=40080; \
		clients=8; \
		duration_ms=2000; \
		tick_hz=60; \
		workers=4; \
		server_duration_ms=$$((duration_ms + 500)); \
		rm -f build/p008_perf_server.log; \
		./build/p008_net_perf_server_tests "$$port" "$$clients" "$$server_duration_ms" "$$tick_hz" "$$workers" > build/p008_perf_server.log 2>&1 & \
		srv_pid=$$!; \
		i=0; \
		while ! grep -q "P008_REPL_SERVER_READY" build/p008_perf_server.log; do \
			i=$$((i+1)); \
			if [ "$$i" -gt 200 ]; then \
				echo "Server did not become ready"; \
				kill "$$srv_pid" >/dev/null 2>&1 || true; \
				cat build/p008_perf_server.log || true; \
				exit 1; \
			fi; \
			sleep 0.01; \
		done; \
		./build/p008_net_perf_client_tests 127.0.0.1 "$$port" "$$clients" "$$duration_ms" "$$tick_hz"; \
		kill "$$srv_pid" >/dev/null 2>&1 || true; \
		wait "$$srv_pid" >/dev/null 2>&1 || true; \
		grep -E "P008_SERVER_STATS|p008 stats:" build/p008_perf_server.log || true \
	'

p008_renderer_client:
	@$(MAKE) build/p008_renderer_client
	@echo "Built: build/p008_renderer_client"
	@echo "Run server:  ./build/p008_net_repl_server 40080 16 0 60 4"
	@echo "Run client:  ./build/p008_renderer_client 127.0.0.1 40080 10000 --seed 123"

clean:
	$(RM) $(BIN)
