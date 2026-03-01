CC ?= clang
JOB_INSTRUMENTATION ?= 0
TRACY ?= 0
STACK_CANARY ?= 0
EMU ?= 0

CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -pthread -Iinclude -Ithird_party/stb -Ithird_party/glad/include -O3
CFLAGS += -DFR_JOB_INSTRUMENTATION=$(JOB_INSTRUMENTATION)
CFLAGS += -DJOB_STACK_CANARY=$(STACK_CANARY)

ifeq ($(EMU),1)
	CFLAGS += -DFR_NET_EMULATION
endif

LDFLAGS ?= -lm

TRACY_DIR := extern/tracy
TRACY_BUILD_DIR := $(TRACY_DIR)/build
TRACY_CLIENT_LIB := $(TRACY_BUILD_DIR)/libTracyClient.a

ifeq ($(TRACY),1)
	ifeq (,$(wildcard $(TRACY_CLIENT_LIB)))
		$(error Tracy client library missing: $(TRACY_CLIENT_LIB) (build extern/tracy first))
	endif
	CFLAGS += -DTRACY_ENABLE -DTRACY_ON_DEMAND -I$(TRACY_DIR)/public
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
ENTITY_SRC := $(wildcard src/entity/*.c)
RENDERER_SRC := $(wildcard src/renderer/*.c) $(wildcard src/renderer/skinning/*.c)
RENDERER_DEBUG_LINES_SRC := $(wildcard src/renderer/debug_lines/*.c)
RENDERER_VIDEO_CAPTURE_SRC := $(wildcard src/renderer/video_capture/*.c)
RENDERER_SRC += $(RENDERER_VIDEO_CAPTURE_SRC)
NET_SRC := $(wildcard src/net/*.c) $(wildcard src/net/udp/*.c) $(wildcard src/net/rudp/*.c) $(wildcard src/net/rudp/reliability/*.c) $(wildcard src/net/rudp/stream/*.c) $(wildcard src/net/quantization/*.c) \
	$(wildcard src/net/replication/*.c) $(wildcard src/net/replication/*/*.c) \
	$(wildcard src/net/test/*.c) $(wildcard src/net/client/*.c) $(wildcard src/net/topic/*.c) $(wildcard src/net/topic/dispatch/*.c) \
	$(wildcard src/net/channel/*.c) $(wildcard src/net/channel/*/*.c) $(wildcard src/net/channel/*/*/*.c) \
	$(wildcard src/net/emulation/*.c)
SERVER_SRC := $(wildcard src/server/repl/repl_server_*.c) $(wildcard src/server/net/fiber/*.c) $(wildcard src/server/net/runtime/*.c) \
	$(wildcard src/server/entity/*.c) $(wildcard src/server/entity/*/*.c) $(wildcard src/server/entity/*/*/*.c) \
	$(wildcard src/server/physics/*.c) $(wildcard src/server/physics/*/*.c) $(wildcard src/server/physics/*/*/*.c) \
	$(wildcard src/server/tick/*.c)
PHYS_SRC := $(wildcard src/physics/*.c) $(wildcard src/physics/*/*.c) $(wildcard src/physics/*/*/*.c)
MESH_SRC := $(wildcard src/mesh/*.c)
ENGINE_SRC := src/engine_settings.c
EDITOR_SRC := $(wildcard src/editor/*.c) $(wildcard src/editor/*/*.c) $(wildcard src/editor/*/*/*.c)
SRC_HEADLESS := $(JOB_SRC) $(MATH_SRC) $(MEM_SRC) $(ECS_SRC) $(ENTITY_SRC) $(NET_SRC) $(SERVER_SRC) $(PHYS_SRC) $(MESH_SRC) $(ENGINE_SRC) $(EDITOR_SRC) $(RENDERER_DEBUG_LINES_SRC)
SRC_ALL := $(SRC_HEADLESS) $(RENDERER_SRC)

# Legacy prerequisite variable used by some build rules.
SRC := $(SRC_HEADLESS)

SDL2_CFLAGS := $(shell sdl2-config --cflags 2>/dev/null)
SDL2_LIBS := $(shell sdl2-config --libs 2>/dev/null)
GLEW_LIBS :=
GL_LIBS := -lGL -ldl
RENDERER_TEST_CFLAGS := $(SDL2_CFLAGS)
RENDERER_TEST_LIBS := $(SDL2_LIBS) -lSDL2 $(GL_LIBS)

# ── Incremental compilation via object files ─────────────────────
# Compile each .c → build/obj/<path>.o, then archive into static libs.
# Changing one .c only recompiles that .o and re-links affected binaries.

OBJDIR := build/obj

OBJ_HEADLESS := $(patsubst %.c,$(OBJDIR)/%.o,$(SRC_HEADLESS))
OBJ_RENDERER := $(patsubst %.c,$(OBJDIR)/%.o,$(RENDERER_SRC))
OBJ_GLAD     := $(OBJDIR)/third_party/glad/src/glad.o
OBJ_ALL      := $(OBJ_HEADLESS) $(OBJ_RENDERER) $(OBJ_GLAD)

# Auto-generate per-file dependency tracking (.d files).
DEPFLAGS = -MMD -MP -MF $(OBJDIR)/$*.d
ALL_DEPS := $(OBJ_ALL:.o=.d)

BIN_HEADLESS := build/p000_tests build/p001_tests build/p002_tests build/p003_tests \
	build/p007_net_tests build/p007_net_header_tests build/p007_net_ack_tests build/p007_net_unreliable_tests \
	build/p007_net_reliable_tests build/p007_net_schema_registry_tests build/p007_net_test_client_api_tests \
	build/p007_net_test_transport_tests build/p007_net_body_state_newest_wins_tests build/p007_net_rudp_fragmentation_tests \
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
	build/p078_physics_occlusion_demotion_tests \
	build/p080_physics_phase4_integration_tests \
	build/p083_physics_position_projection_tests \
	build/p084_physics_raycast_tests \
	build/p085_physics_overlap_tests \
	build/p086_physics_closest_point_tests \
	build/p087_physics_phase5_integration_tests \
	build/p088_physics_static_bvh_build_tests \
	build/p089_physics_static_bvh_query_tests \
	build/p090_physics_static_bvh_rebuild_tests \
	build/p091_physics_phase6_integration_tests \
	build/p007_net_udp_socket_tests build/p007_net_integration_server_tests build/p007_net_integration_client_tests \
	build/p007_net_rtt_retransmit_tests \
	build/p007_net_ghost_table_tests \
	build/p007_net_snapshot_delta_tests \
	build/p007_net_snapshot_chunk_tests \
	build/p007_net_interest_tests \
	build/p007_net_time_sync_tests \
	build/p007_net_prediction_tests \
	build/p007_net_validation_tests \
	build/p007_net_integration_tests \
	build/p007_net_client_tx_tests \
	build/p008_net_repl_server build/p008_net_repl_client build/p008_net_multi_client_server_integration_tests \
	build/p008_net_perf_server_tests build/p008_net_perf_client_tests \
	build/p000_job_performance_tests build/p002_memory_apool_tests build/p007_net_topic_dispatch_tests build/p007_net_topic_dispatch_benchmark \
	build/p008_server_compute_jobs_tests build/p007_net_stream_api_tests build/p007_net_stream_flush_send_tests build/p007_net_stream_channel_topic_tests \
	build/p008_server_client_fiber_stream_tests build/p008_server_net_runtime_fiber_tests \
	build/p008_server_entity_net_pump_tests \
	build/p008_server_body_state_broadcast_tests \
	build/p008_net_join_spawn_integration_tests \
	build/p008_net_rudp_loss_convergence_tests \
	build/p008_pose_interpolator_tests \
	build/p009_server_state_update_queue_tests \
	build/p009_net_topic_channel_ring_tests \
	build/p011_renderer_correction_debug_lines_tests \
	build/p000_job_queue_diagnostics_tests \
	build/p000_ws_deque_tests \
	build/p092_server_pre_physics_sync_tests \
	build/p093_island_tier_promote_tests \
	build/p094_xpbd_dispatch_tests \
	build/p095_constraint_color_tests \
	build/p096_tgs_coloring_tests \
	build/p097_speculative_contact_tests \
	build/p098_island_split_tests \
	build/p099_physics_joint_tests \
	build/p100_physics_joint_constraint_tests \
	build/p101_physics_joint_island_tests \
	build/p102_physics_joint_integration_tests \
	build/p103_net_emulator_tests \
	build/p104_engine_settings_tests \
	build/p105_variable_dt_tests \
	build/p106_video_capture_frame_ring_tests \
	build/p107_mesh_collider_bvh_tests \
	build/p108_mesh_narrowphase_tests \
	build/p109_mesh_integration_tests \
	build/p110_obj_loader_tests \
	build/p111_ccd_tests \
	build/p112_halfspace_tests \
	build/p113_convex_hull_tests \
	build/p115_gjk_epa_tests \
	build/p116_convex_narrowphase_tests \
	build/p117_convex_decompose_tests \
	build/p118_compound_collider_tests \
	build/p119_snapshot_interp_tests \
	build/p008_server_tick_loop_tests \
	build/p008_server_tick_encoder_tests \
	build/p008_server_loop_integration_tests

ifeq ($(TRACY),1)
BIN_HEADLESS += build/p010_tracy_alloc_override_tests
endif


BIN_HEADLESS += build/entity_attrs_tests
BIN_HEADLESS += build/edit_script_env_tests
BIN_HEADLESS += build/aegis_types_tests
BIN_HEADLESS += build/aegis_memory_tests
BIN_HEADLESS += build/aegis_decode_tests
BIN_HEADLESS += build/aegis_ops_arith_tests
BIN_HEADLESS += build/aegis_ops_flow_tests
BIN_HEADLESS += build/aegis_ops_data_tests
BIN_HEADLESS += build/aegis_ops_math_tests
BIN_HEADLESS += build/aegis_yield_tests
BIN_HEADLESS += build/aegis_vm_tests
BIN_HEADLESS += build/aegis_vm_math_stress_tests
BIN_HEADLESS += build/aegis_vm_memory_exhaust_tests
BIN_HEADLESS += build/aegis_vm_interrupt_tests
BIN_HEADLESS += build/aegis_event_tests
BIN_HEADLESS += build/aegis_ops_event_tests
BIN_HEADLESS += build/aegis_asm_tests
BIN_HEADLESS += build/aegis_runtime_tests

BIN_RENDERER_TESTS := build/p004_tests build/p004_shader_tests build/p004_buffer_tests \
	build/p004_uniform_tests build/p004_palette_tests build/p004_pipeline_tests \
	build/p004_skinning_tests build/p004_ecs_skinning_tests build/p004_skinning_alloc_tests \
	build/p004_pipeline_resource_tests build/p004_pipeline_graph_tests

BIN := $(BIN_HEADLESS) $(BIN_RENDERER_TESTS)

.PHONY: all test test_renderer clean p008_build p008_test p008_help p008_perf p008_renderer_client demo_server demo_client editor_tui

all: $(BIN)

# Include auto-generated dependency files (after default target to avoid
# .d file targets from overriding the default goal).
-include $(ALL_DEPS)

# ── Object compilation rules (after default target) ──────────────

# Pattern rule: src/foo/bar.c → build/obj/src/foo/bar.o
$(OBJDIR)/%.o: %.c | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

# Renderer sources need SDL2 flags.
$(patsubst %.c,$(OBJDIR)/%.o,$(RENDERER_SRC)): $(OBJDIR)/%.o: %.c | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(RENDERER_TEST_CFLAGS) $(DEPFLAGS) -c $< -o $@

# GLAD loader (third-party, suppress warnings).
$(OBJ_GLAD): third_party/glad/src/glad.c | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -w -c $< -o $@

# Static libraries.
build/libheadless.a: $(OBJ_HEADLESS) | build
	$(AR) rcs $@ $?

build/liball.a: $(OBJ_ALL) | build
	$(AR) rcs $@ $?

# ── Binary targets ───────────────────────────────────────────────

build/p000_tests: build/libheadless.a tests/p000_fiber_job_system_tests.c | build
	$(CC) $(CFLAGS) tests/p000_fiber_job_system_tests.c build/libheadless.a -o $@ $(LDFLAGS)

## AddressSanitizer does not support custom fiber stacks without special hooks.
## Build the perf harness without ASan to avoid false-positive crashes.
CFLAGS_NO_ASAN := $(filter-out -fsanitize=address,$(CFLAGS))
build/p000_job_performance_tests: build/libheadless.a tests/p000_job_performance_tests.c | build
	$(CC) $(CFLAGS_NO_ASAN) tests/p000_job_performance_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p001_tests: build/libheadless.a tests/p001_core_math_tests.c | build
	$(CC) $(CFLAGS) tests/p001_core_math_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p002_tests: build/libheadless.a tests/p002_memory_tests.c | build
	$(CC) $(CFLAGS) tests/p002_memory_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p002_memory_apool_tests: build/libheadless.a tests/p002_memory_apool_tests.c | build
	$(CC) $(CFLAGS) tests/p002_memory_apool_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p003_tests: build/libheadless.a tests/p003_ecs_tests.c | build
	$(CC) $(CFLAGS) tests/p003_ecs_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p000_job_queue_sharding_tests: build/libheadless.a tests/p000_job_queue_sharding_tests.c | build
	$(CC) $(CFLAGS) tests/p000_job_queue_sharding_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p000_job_queue_diagnostics_tests: build/libheadless.a tests/p000_job_queue_diagnostics_tests.c | build
	$(CC) $(CFLAGS) tests/p000_job_queue_diagnostics_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p000_ws_deque_tests: build/libheadless.a tests/p000_ws_deque_tests.c | build
	$(CC) $(CFLAGS) tests/p000_ws_deque_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p007_net_tests: build/libheadless.a tests/p007_net_test_utils_tests.c | build
	$(CC) $(CFLAGS) tests/p007_net_test_utils_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p007_net_header_tests: build/libheadless.a tests/p007_net_header_tests.c | build
	$(CC) $(CFLAGS) tests/p007_net_header_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p007_net_ack_tests: build/libheadless.a tests/p007_net_ack_tests.c | build
	$(CC) $(CFLAGS) tests/p007_net_ack_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p007_net_unreliable_tests: build/libheadless.a tests/p007_net_unreliable_tests.c | build
	$(CC) $(CFLAGS) tests/p007_net_unreliable_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p007_net_schema_registry_tests: build/libheadless.a tests/p007_net_schema_registry_tests.c | build
	$(CC) $(CFLAGS) tests/p007_net_schema_registry_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p007_net_test_client_api_tests: build/libheadless.a tests/p007_net_test_client_api_tests.c | build
	$(CC) $(CFLAGS) tests/p007_net_test_client_api_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p007_net_test_transport_tests: build/libheadless.a tests/p007_net_test_transport_tests.c | build
	$(CC) $(CFLAGS) tests/p007_net_test_transport_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p007_net_body_state_newest_wins_tests: build/libheadless.a tests/p007_net_body_state_newest_wins_tests.c | build
	$(CC) $(CFLAGS) tests/p007_net_body_state_newest_wins_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p007_net_reliable_tests: build/libheadless.a tests/p007_net_reliable_tests.c | build
	$(CC) $(CFLAGS) tests/p007_net_reliable_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p007_net_rudp_fragmentation_tests: build/libheadless.a tests/net_rudp_fragmentation_tests.c | build
	$(CC) $(CFLAGS) tests/net_rudp_fragmentation_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p012_net_rudp_reliability_boundary_tests: build/libheadless.a tests/p012_net_rudp_reliability_boundary_tests.c | build
	$(CC) $(CFLAGS) tests/p012_net_rudp_reliability_boundary_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p013_net_rudp_reliability_layer_tests: build/libheadless.a tests/p013_net_rudp_reliability_layer_tests.c | build
	$(CC) $(CFLAGS) tests/p013_net_rudp_reliability_layer_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p014_net_rudp_reliability_send_layer_tests: build/libheadless.a tests/p014_net_rudp_reliability_send_layer_tests.c | build
	$(CC) $(CFLAGS) tests/p014_net_rudp_reliability_send_layer_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p015_server_net_inbound_message_tests: build/libheadless.a tests/p015_server_net_inbound_message_tests.c | build
	$(CC) $(CFLAGS) tests/p015_server_net_inbound_message_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p016_net_repl_input_rot_tests: build/libheadless.a tests/p016_net_repl_input_rot_tests.c | build
	$(CC) $(CFLAGS) tests/p016_net_repl_input_rot_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p017_math_quat_angle_tests: build/libheadless.a tests/p017_math_quat_angle_tests.c | build
	$(CC) $(CFLAGS) tests/p017_math_quat_angle_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p018_physics_types_tests: build/libheadless.a tests/p018_physics_types_tests.c | build
	$(CC) $(CFLAGS) tests/p018_physics_types_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p019_physics_body_tests: build/libheadless.a tests/p019_physics_body_tests.c | build
	$(CC) $(CFLAGS) tests/p019_physics_body_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p020_physics_collider_tests: build/libheadless.a tests/p020_physics_collider_tests.c | build
	$(CC) $(CFLAGS) tests/p020_physics_collider_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p021_physics_aabb_tests: build/libheadless.a tests/p021_physics_aabb_tests.c | build
	$(CC) $(CFLAGS) tests/p021_physics_aabb_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p022_physics_pool_arena_tests: build/libheadless.a tests/p022_physics_pool_arena_tests.c | build
	$(CC) $(CFLAGS) tests/p022_physics_pool_arena_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p023_physics_manifold_tests: build/libheadless.a tests/p023_physics_manifold_tests.c | build
	$(CC) $(CFLAGS) tests/p023_physics_manifold_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p024_physics_constraint_tests: build/libheadless.a tests/p024_physics_constraint_tests.c | build
	$(CC) $(CFLAGS) tests/p024_physics_constraint_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p025_physics_game_state_tests: build/libheadless.a tests/p025_physics_game_state_tests.c | build
	$(CC) $(CFLAGS) tests/p025_physics_game_state_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p026_physics_compound_collider_tests: build/libheadless.a tests/p026_physics_compound_collider_tests.c | build
	$(CC) $(CFLAGS) tests/p026_physics_compound_collider_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p027_physics_tier_list_tests: build/libheadless.a tests/p027_physics_tier_list_tests.c | build
	$(CC) $(CFLAGS) tests/p027_physics_tier_list_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p028_physics_spatial_grid_tests: build/libheadless.a tests/p028_physics_spatial_grid_tests.c | build
	$(CC) $(CFLAGS) tests/p028_physics_spatial_grid_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p029_physics_manifold_cache_tests: build/libheadless.a tests/p029_physics_manifold_cache_tests.c | build
	$(CC) $(CFLAGS) tests/p029_physics_manifold_cache_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p030_physics_island_tests: build/libheadless.a tests/p030_physics_island_tests.c | build
	$(CC) $(CFLAGS) tests/p030_physics_island_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p031_physics_world_tests: build/libheadless.a tests/p031_physics_world_tests.c | build
	$(CC) $(CFLAGS) tests/p031_physics_world_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p032_physics_phase0_integration_tests: build/libheadless.a tests/p032_physics_phase0_integration_tests.c | build
	$(CC) $(CFLAGS) tests/p032_physics_phase0_integration_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p033_physics_step_plan_tests: build/libheadless.a tests/p033_physics_step_plan_tests.c | build
	$(CC) $(CFLAGS) tests/p033_physics_step_plan_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p034_physics_tier_classify_tests: build/libheadless.a tests/p034_physics_tier_classify_tests.c | build
	$(CC) $(CFLAGS) tests/p034_physics_tier_classify_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p035_physics_spatial_update_tests: build/libheadless.a tests/p035_physics_spatial_update_tests.c | build
	$(CC) $(CFLAGS) tests/p035_physics_spatial_update_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p036_physics_halo_closure_tests: build/libheadless.a tests/p036_physics_halo_closure_tests.c | build
	$(CC) $(CFLAGS) tests/p036_physics_halo_closure_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p037_physics_aabb_update_tests: build/libheadless.a tests/p037_physics_aabb_update_tests.c | build
	$(CC) $(CFLAGS) tests/p037_physics_aabb_update_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p038_physics_broadphase_tests: build/libheadless.a tests/p038_physics_broadphase_tests.c | build
	$(CC) $(CFLAGS) tests/p038_physics_broadphase_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p039_physics_narrowphase_tests: build/libheadless.a tests/p039_physics_narrowphase_tests.c | build
	$(CC) $(CFLAGS) tests/p039_physics_narrowphase_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p040_physics_manifold_build_tests: build/libheadless.a tests/p040_physics_manifold_build_tests.c | build
	$(CC) $(CFLAGS) tests/p040_physics_manifold_build_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p041_physics_stabilization_tests: build/libheadless.a tests/p041_physics_stabilization_tests.c | build
	$(CC) $(CFLAGS) tests/p041_physics_stabilization_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p042_physics_constraint_build_tests: build/libheadless.a tests/p042_physics_constraint_build_tests.c | build
	$(CC) $(CFLAGS) tests/p042_physics_constraint_build_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p043_physics_island_build_tests: build/libheadless.a tests/p043_physics_island_build_tests.c | build
	$(CC) $(CFLAGS) tests/p043_physics_island_build_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p044_physics_tgs_solve_tests: build/libheadless.a tests/p044_physics_tgs_solve_tests.c | build
	$(CC) $(CFLAGS) tests/p044_physics_tgs_solve_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p045_physics_xpbd_solve_tests: build/libheadless.a tests/p045_physics_xpbd_solve_tests.c | build
	$(CC) $(CFLAGS) tests/p045_physics_xpbd_solve_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p046_physics_solver_transition_tests: build/libheadless.a tests/p046_physics_solver_transition_tests.c | build
	$(CC) $(CFLAGS) tests/p046_physics_solver_transition_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p047_physics_integrate_tests: build/libheadless.a tests/p047_physics_integrate_tests.c | build
	$(CC) $(CFLAGS) tests/p047_physics_integrate_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p048_physics_cache_commit_tests: build/libheadless.a tests/p048_physics_cache_commit_tests.c | build
	$(CC) $(CFLAGS) tests/p048_physics_cache_commit_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p049_physics_tick_tests: build/libheadless.a tests/p049_physics_tick_tests.c | build
	$(CC) $(CFLAGS) tests/p049_physics_tick_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p050_physics_impact_event_tests: build/libheadless.a tests/p050_physics_impact_event_tests.c | build
	$(CC) $(CFLAGS) tests/p050_physics_impact_event_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p051_physics_snapshot_tests: build/libheadless.a tests/p051_physics_snapshot_tests.c | build
	$(CC) $(CFLAGS) tests/p051_physics_snapshot_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p052_physics_prediction_tests: build/libheadless.a tests/p052_physics_prediction_tests.c | build
	$(CC) $(CFLAGS) tests/p052_physics_prediction_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p053_physics_phase1_integration_tests: build/libheadless.a tests/p053_physics_phase1_integration_tests.c | build
	$(CC) $(CFLAGS) tests/p053_physics_phase1_integration_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p054_physics_sphere_box_tests: build/libheadless.a tests/p054_physics_sphere_box_tests.c | build
	$(CC) $(CFLAGS) tests/p054_physics_sphere_box_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p055_physics_sphere_capsule_tests: build/libheadless.a tests/p055_physics_sphere_capsule_tests.c | build
	$(CC) $(CFLAGS) tests/p055_physics_sphere_capsule_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p056_physics_box_box_tests: build/libheadless.a tests/p056_physics_box_box_tests.c | build
	$(CC) $(CFLAGS) tests/p056_physics_box_box_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p057_physics_box_capsule_tests: build/libheadless.a tests/p057_physics_box_capsule_tests.c | build
	$(CC) $(CFLAGS) tests/p057_physics_box_capsule_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p058_physics_capsule_capsule_tests: build/libheadless.a tests/p058_physics_capsule_capsule_tests.c | build
	$(CC) $(CFLAGS) tests/p058_physics_capsule_capsule_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p059_physics_phase2_integration_tests: build/libheadless.a tests/p059_physics_phase2_integration_tests.c | build
	$(CC) $(CFLAGS) tests/p059_physics_phase2_integration_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p060_physics_job_infra_tests: build/libheadless.a tests/p060_physics_job_infra_tests.c | build
	$(CC) $(CFLAGS) tests/p060_physics_job_infra_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p061_physics_par_tier_tests: build/libheadless.a tests/p061_physics_par_tier_tests.c | build
	$(CC) $(CFLAGS) tests/p061_physics_par_tier_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p062_physics_par_spatial_tests: build/libheadless.a tests/p062_physics_par_spatial_tests.c | build
	$(CC) $(CFLAGS) tests/p062_physics_par_spatial_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p063_physics_par_broadphase_tests: build/libheadless.a tests/p063_physics_par_broadphase_tests.c | build
	$(CC) $(CFLAGS) tests/p063_physics_par_broadphase_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p064_physics_par_narrowphase_tests: build/libheadless.a tests/p064_physics_par_narrowphase_tests.c | build
	$(CC) $(CFLAGS) tests/p064_physics_par_narrowphase_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p065_physics_par_manifold_tests: build/libheadless.a tests/p065_physics_par_manifold_tests.c | build
	$(CC) $(CFLAGS) tests/p065_physics_par_manifold_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p066_physics_par_stabilization_tests: build/libheadless.a tests/p066_physics_par_stabilization_tests.c | build
	$(CC) $(CFLAGS) tests/p066_physics_par_stabilization_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p067_physics_par_constraint_tests: build/libheadless.a tests/p067_physics_par_constraint_tests.c | build
	$(CC) $(CFLAGS) tests/p067_physics_par_constraint_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p068_physics_par_tgs_tests: build/libheadless.a tests/p068_physics_par_tgs_tests.c | build
	$(CC) $(CFLAGS) tests/p068_physics_par_tgs_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p069_physics_par_xpbd_tests: build/libheadless.a tests/p069_physics_par_xpbd_tests.c | build
	$(CC) $(CFLAGS) tests/p069_physics_par_xpbd_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p070_physics_par_integrate_tests: build/libheadless.a tests/p070_physics_par_integrate_tests.c | build
	$(CC) $(CFLAGS) tests/p070_physics_par_integrate_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p071_physics_par_tick_tests: build/libheadless.a tests/p071_physics_par_tick_tests.c | build
	$(CC) $(CFLAGS) tests/p071_physics_par_tick_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p072_physics_phase3_integration_tests: build/libheadless.a tests/p072_physics_phase3_integration_tests.c | build
	$(CC) $(CFLAGS) tests/p072_physics_phase3_integration_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p073_physics_tier_distance_tests: build/libheadless.a tests/p073_physics_tier_distance_tests.c | build
	$(CC) $(CFLAGS) tests/p073_physics_tier_distance_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p074_physics_tier_params_tests: build/libheadless.a tests/p074_physics_tier_params_tests.c | build
	$(CC) $(CFLAGS) tests/p074_physics_tier_params_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p075_physics_solver_transition_tests: build/libheadless.a tests/p075_physics_solver_transition_tests.c | build
	$(CC) $(CFLAGS) tests/p075_physics_solver_transition_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p076_physics_tier_stabilization_tests: build/libheadless.a tests/p076_physics_tier_stabilization_tests.c | build
	$(CC) $(CFLAGS) tests/p076_physics_tier_stabilization_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p077_physics_amortized_t4_tests: build/libheadless.a tests/p077_physics_amortized_t4_tests.c | build
	$(CC) $(CFLAGS) tests/p077_physics_amortized_t4_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p079_physics_sphere_simplify_tests: build/libheadless.a tests/p079_physics_sphere_simplify_tests.c | build
	$(CC) $(CFLAGS) tests/p079_physics_sphere_simplify_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p078_physics_occlusion_demotion_tests: build/libheadless.a tests/p078_physics_occlusion_demotion_tests.c | build
	$(CC) $(CFLAGS) tests/p078_physics_occlusion_demotion_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p080_physics_phase4_integration_tests: build/libheadless.a tests/p080_physics_phase4_integration_tests.c | build
	$(CC) $(CFLAGS) tests/p080_physics_phase4_integration_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p083_physics_position_projection_tests: build/libheadless.a tests/p083_physics_position_projection_tests.c | build
	$(CC) $(CFLAGS) tests/p083_physics_position_projection_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p084_physics_raycast_tests: build/libheadless.a tests/p084_physics_raycast_tests.c | build
	$(CC) $(CFLAGS) tests/p084_physics_raycast_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p085_physics_overlap_tests: build/libheadless.a tests/p085_physics_overlap_tests.c | build
	$(CC) $(CFLAGS) tests/p085_physics_overlap_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p086_physics_closest_point_tests: build/libheadless.a tests/p086_physics_closest_point_tests.c | build
	$(CC) $(CFLAGS) tests/p086_physics_closest_point_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p087_physics_phase5_integration_tests: build/libheadless.a tests/p087_physics_phase5_integration_tests.c | build
	$(CC) $(CFLAGS) tests/p087_physics_phase5_integration_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p087_physics_phase5_benchmarks: build/libheadless.a tests/p087_physics_phase5_benchmarks.c | build
	$(CC) $(CFLAGS) tests/p087_physics_phase5_benchmarks.c build/libheadless.a -o $@ $(LDFLAGS)

build/p088_physics_static_bvh_build_tests: build/libheadless.a tests/p088_physics_static_bvh_build_tests.c | build
	$(CC) $(CFLAGS) tests/p088_physics_static_bvh_build_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p088_physics_static_bvh_build_benchmarks: build/libheadless.a tests/p088_physics_static_bvh_build_benchmarks.c | build
	$(CC) $(CFLAGS) tests/p088_physics_static_bvh_build_benchmarks.c build/libheadless.a -o $@ $(LDFLAGS)

build/p089_physics_static_bvh_query_tests: build/libheadless.a tests/p089_physics_static_bvh_query_tests.c | build
	$(CC) $(CFLAGS) tests/p089_physics_static_bvh_query_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p090_physics_static_bvh_rebuild_tests: build/libheadless.a tests/p090_physics_static_bvh_rebuild_tests.c | build
	$(CC) $(CFLAGS) tests/p090_physics_static_bvh_rebuild_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p091_physics_phase6_integration_tests: build/libheadless.a tests/p091_physics_phase6_integration_tests.c | build
	$(CC) $(CFLAGS) tests/p091_physics_phase6_integration_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p091_physics_phase6_benchmarks: build/libheadless.a tests/p091_physics_phase6_benchmarks.c | build
	$(CC) $(CFLAGS) tests/p091_physics_phase6_benchmarks.c build/libheadless.a -o $@ $(LDFLAGS)

build/p092_server_pre_physics_sync_tests: build/libheadless.a tests/p092_server_pre_physics_sync_tests.c | build
	$(CC) $(CFLAGS) tests/p092_server_pre_physics_sync_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p093_island_tier_promote_tests: build/libheadless.a tests/p093_island_tier_promote_tests.c | build
	$(CC) $(CFLAGS) tests/p093_island_tier_promote_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p094_xpbd_dispatch_tests: build/libheadless.a tests/p094_xpbd_dispatch_tests.c | build
	$(CC) $(CFLAGS) tests/p094_xpbd_dispatch_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p095_constraint_color_tests: build/libheadless.a tests/p095_constraint_color_tests.c | build
	$(CC) $(CFLAGS) tests/p095_constraint_color_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p096_tgs_coloring_tests: build/libheadless.a tests/p096_tgs_coloring_tests.c | build
	$(CC) $(CFLAGS) tests/p096_tgs_coloring_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p097_speculative_contact_tests: build/libheadless.a tests/p097_speculative_contact_tests.c | build
	$(CC) $(CFLAGS) tests/p097_speculative_contact_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p098_island_split_tests: build/libheadless.a tests/p098_island_split_tests.c | build
	$(CC) $(CFLAGS) tests/p098_island_split_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p099_physics_joint_tests: build/libheadless.a tests/p099_physics_joint_tests.c | build
	$(CC) $(CFLAGS) tests/p099_physics_joint_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p100_physics_joint_constraint_tests: build/libheadless.a tests/p100_physics_joint_constraint_tests.c | build
	$(CC) $(CFLAGS) tests/p100_physics_joint_constraint_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p101_physics_joint_island_tests: build/libheadless.a tests/p101_physics_joint_island_tests.c | build
	$(CC) $(CFLAGS) tests/p101_physics_joint_island_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p102_physics_joint_integration_tests: build/libheadless.a tests/p102_physics_joint_integration_tests.c | build
	$(CC) $(CFLAGS) tests/p102_physics_joint_integration_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p103_net_emulator_tests: build/libheadless.a tests/p103_net_emulator_tests.c | build
	$(CC) $(CFLAGS) tests/p103_net_emulator_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p104_engine_settings_tests: build/libheadless.a tests/p104_engine_settings_tests.c | build
	$(CC) $(CFLAGS) tests/p104_engine_settings_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p105_variable_dt_tests: build/libheadless.a tests/p105_variable_dt_tests.c | build
	$(CC) $(CFLAGS) tests/p105_variable_dt_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p106_video_capture_frame_ring_tests: tests/p106_video_capture_frame_ring_tests.c src/renderer/video_capture/frame_ring.c | build
	$(CC) $(CFLAGS) tests/p106_video_capture_frame_ring_tests.c src/renderer/video_capture/frame_ring.c -o $@ $(LDFLAGS)

build/p107_mesh_collider_bvh_tests: build/libheadless.a tests/p107_mesh_collider_bvh_tests.c | build
	$(CC) $(CFLAGS) tests/p107_mesh_collider_bvh_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p108_mesh_narrowphase_tests: build/libheadless.a tests/p108_mesh_narrowphase_tests.c | build
	$(CC) $(CFLAGS) tests/p108_mesh_narrowphase_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p109_mesh_integration_tests: build/libheadless.a tests/p109_mesh_integration_tests.c | build
	$(CC) $(CFLAGS) tests/p109_mesh_integration_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p110_obj_loader_tests: build/libheadless.a tests/p110_obj_loader_tests.c | build
	$(CC) $(CFLAGS) tests/p110_obj_loader_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p111_ccd_tests: build/libheadless.a tests/p111_ccd_tests.c | build
	$(CC) $(CFLAGS) tests/p111_ccd_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p112_halfspace_tests: build/libheadless.a tests/p112_halfspace_tests.c | build
	$(CC) $(CFLAGS) tests/p112_halfspace_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p113_convex_hull_tests: build/libheadless.a tests/p113_convex_hull_tests.c | build
	$(CC) $(CFLAGS) tests/p113_convex_hull_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p115_gjk_epa_tests: build/libheadless.a tests/p115_gjk_epa_tests.c | build
	$(CC) $(CFLAGS) tests/p115_gjk_epa_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p116_convex_narrowphase_tests: build/libheadless.a tests/p116_convex_narrowphase_tests.c | build
	$(CC) $(CFLAGS) tests/p116_convex_narrowphase_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p117_convex_decompose_tests: build/libheadless.a tests/p117_convex_decompose_tests.c | build
	$(CC) $(CFLAGS) tests/p117_convex_decompose_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p118_compound_collider_tests: build/libheadless.a tests/p118_compound_collider_tests.c | build
	$(CC) $(CFLAGS) tests/p118_compound_collider_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p119_snapshot_interp_tests: build/libheadless.a tests/p119_snapshot_interp_tests.c | build
	$(CC) $(CFLAGS) tests/p119_snapshot_interp_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p007_net_udp_socket_tests: build/libheadless.a tests/p007_net_udp_socket_tests.c | build
	$(CC) $(CFLAGS) tests/p007_net_udp_socket_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p007_net_rtt_retransmit_tests: build/libheadless.a tests/p007_net_rtt_retransmit_tests.c | build
	$(CC) $(CFLAGS) tests/p007_net_rtt_retransmit_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p007_net_ghost_table_tests: build/libheadless.a tests/p007_net_ghost_table_tests.c | build
	$(CC) $(CFLAGS) tests/p007_net_ghost_table_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p007_net_snapshot_delta_tests: build/libheadless.a tests/p007_net_snapshot_delta_tests.c | build
	$(CC) $(CFLAGS) tests/p007_net_snapshot_delta_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p007_net_snapshot_chunk_tests: build/libheadless.a tests/p007_net_snapshot_chunk_tests.c | build
	$(CC) $(CFLAGS) tests/p007_net_snapshot_chunk_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p007_net_interest_tests: build/libheadless.a tests/p007_net_interest_tests.c | build
	$(CC) $(CFLAGS) tests/p007_net_interest_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p007_net_time_sync_tests: build/libheadless.a tests/p007_net_time_sync_tests.c | build
	$(CC) $(CFLAGS) tests/p007_net_time_sync_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p007_net_prediction_tests: build/libheadless.a tests/p007_net_prediction_tests.c | build
	$(CC) $(CFLAGS) tests/p007_net_prediction_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p007_net_validation_tests: build/libheadless.a tests/p007_net_validation_tests.c | build
	$(CC) $(CFLAGS) tests/p007_net_validation_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p007_net_integration_tests: build/libheadless.a tests/p007_net_integration_tests.c | build
	$(CC) $(CFLAGS) tests/p007_net_integration_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p007_net_integration_server_tests: build/libheadless.a tests/p007_net_integration_server_tests.c | build
	$(CC) $(CFLAGS) tests/p007_net_integration_server_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p007_net_integration_client_tests: build/libheadless.a tests/p007_net_integration_client_tests.c | build
	$(CC) $(CFLAGS) tests/p007_net_integration_client_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p007_net_client_rx_tests: build/libheadless.a tests/p007_net_client_rx_tests.c | build
	$(CC) $(CFLAGS) tests/p007_net_client_rx_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p007_net_client_tx_tests: build/libheadless.a tests/p007_net_client_tx_tests.c | build
	$(CC) $(CFLAGS) tests/p007_net_client_tx_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p007_net_client_rx_udp_topic_tests: build/libheadless.a tests/p007_net_client_rx_udp_topic_tests.c | build
	$(CC) $(CFLAGS) tests/p007_net_client_rx_udp_topic_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p007_net_topic_dispatch_tests: build/libheadless.a tests/p007_net_topic_dispatch_tests.c | build
	$(CC) $(CFLAGS) tests/p007_net_topic_dispatch_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p008_pose_interpolator_tests: build/libheadless.a tests/p008_pose_interpolator_tests.c | build
	$(CC) $(CFLAGS) tests/p008_pose_interpolator_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p007_net_topic_dispatch_benchmark: build/libheadless.a tests/p007_net_topic_dispatch_benchmark.c | build
	$(CC) $(CFLAGS) tests/p007_net_topic_dispatch_benchmark.c build/libheadless.a -o $@ $(LDFLAGS)

build/p007_net_stream_api_tests: build/libheadless.a tests/p007_net_stream_api_tests.c | build
	$(CC) $(CFLAGS) tests/p007_net_stream_api_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p007_net_stream_flush_send_tests: build/libheadless.a tests/p007_net_stream_flush_send_tests.c | build
	$(CC) $(CFLAGS) tests/p007_net_stream_flush_send_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p007_net_stream_channel_topic_tests: build/libheadless.a tests/p007_net_stream_channel_topic_tests.c | build
	$(CC) $(CFLAGS) tests/p007_net_stream_channel_topic_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p007_net_stream_perf_benchmark: build/libheadless.a tests/p007_net_stream_perf_benchmark.c | build
	$(CC) $(CFLAGS) tests/p007_net_stream_perf_benchmark.c build/libheadless.a -o $@ $(LDFLAGS)

build/p008_server_client_fiber_stream_tests: build/libheadless.a tests/p008_server_client_fiber_stream_tests.c | build
	$(CC) $(CFLAGS) tests/p008_server_client_fiber_stream_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p008_server_net_runtime_fiber_tests: build/libheadless.a tests/p008_server_net_runtime_fiber_tests.c | build
	$(CC) $(CFLAGS) tests/p008_server_net_runtime_fiber_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p008_server_entity_net_pump_tests: build/libheadless.a tests/p008_server_entity_net_pump_tests.c | build
	$(CC) $(CFLAGS) tests/p008_server_entity_net_pump_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p008_server_body_state_broadcast_tests: build/libheadless.a tests/p008_server_body_state_broadcast_tests.c | build
	$(CC) $(CFLAGS) tests/p008_server_body_state_broadcast_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p008_server_tick_loop_tests: build/libheadless.a tests/p008_server_tick_loop_tests.c | build
	$(CC) $(CFLAGS) tests/p008_server_tick_loop_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p008_server_tick_encoder_tests: build/libheadless.a tests/p008_server_tick_encoder_tests.c | build
	$(CC) $(CFLAGS) tests/p008_server_tick_encoder_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p008_server_loop_integration_tests: build/libheadless.a tests/p008_server_loop_integration_tests.c | build
	$(CC) $(CFLAGS) tests/p008_server_loop_integration_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p008_net_join_spawn_integration_tests: build/libheadless.a tests/p008_net_join_spawn_integration_tests.c | build
	$(CC) $(CFLAGS) tests/p008_net_join_spawn_integration_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p008_net_rudp_loss_convergence_tests: build/libheadless.a tests/p008_net_rudp_loss_convergence_tests.c | build
	$(CC) $(CFLAGS) tests/p008_net_rudp_loss_convergence_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p008_net_repl_server: build/libheadless.a tests/p008_net_repl_server.c | build
	$(CC) $(CFLAGS) tests/p008_net_repl_server.c build/libheadless.a -o $@ $(LDFLAGS)

build/p008_net_repl_client: build/libheadless.a tests/p008_net_repl_client.c | build
	$(CC) $(CFLAGS) tests/p008_net_repl_client.c build/libheadless.a -o $@ $(LDFLAGS)

build/p008_net_multi_client_server_integration_tests: build/libheadless.a tests/p008_net_multi_client_server_integration_tests.c | build
	$(CC) $(CFLAGS) tests/p008_net_multi_client_server_integration_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p008_net_perf_server_tests: build/libheadless.a tests/p008_net_perf_server_tests.c | build
	$(CC) $(CFLAGS) tests/p008_net_perf_server_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p008_net_perf_client_tests: build/libheadless.a tests/p008_net_perf_client_tests.c | build
	$(CC) $(CFLAGS) tests/p008_net_perf_client_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p008_renderer_client: build/liball.a tests/p008_renderer_client.c | build
	$(CC) $(CFLAGS) $(RENDERER_TEST_CFLAGS) tests/p008_renderer_client.c build/liball.a -o $@ $(LDFLAGS) $(RENDERER_TEST_LIBS)

build/p008_server_compute_jobs_tests: build/libheadless.a tests/p008_server_compute_jobs_tests.c | build
	$(CC) $(CFLAGS) tests/p008_server_compute_jobs_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p009_server_state_update_queue_tests: build/libheadless.a tests/p009_server_state_update_queue_tests.c | build
	$(CC) $(CFLAGS) tests/p009_server_state_update_queue_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p009_net_topic_channel_ring_tests: build/libheadless.a tests/p009_net_topic_channel_ring_tests.c | build
	$(CC) $(CFLAGS) tests/p009_net_topic_channel_ring_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p010_tracy_alloc_override_tests: build/libheadless.a tests/p010_tracy_alloc_override_tests.c | build
	$(CC) $(CFLAGS) tests/p010_tracy_alloc_override_tests.c build/libheadless.a -o $@ $(LDFLAGS)

ENTITY_ATTRS_TEST_SRC := tests/entity/entity_attrs_tests.c $(wildcard src/entity/*.c)
build/entity_attrs_tests: $(ENTITY_ATTRS_TEST_SRC) include/ferrum/entity/entity_attrs.h | build
	$(CC) $(CFLAGS) tests/entity/entity_attrs_tests.c $(wildcard src/entity/*.c) -o $@ $(LDFLAGS)

build/edit_script_env_tests: tests/editor/edit_script_env_tests.c build/libheadless.a | build
	$(CC) $(CFLAGS) tests/editor/edit_script_env_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/aegis_types_tests: tests/aegis/test_aegis_types.c include/ferrum/aegis/aegis_types.h include/ferrum/aegis/aegis_bytecode.h include/ferrum/aegis/aegis_config.h | build
	$(CC) $(CFLAGS) tests/aegis/test_aegis_types.c -o $@ $(LDFLAGS)

build/aegis_memory_tests: tests/aegis/aegis_memory_tests.c src/aegis/aegis_memory.c include/ferrum/aegis/aegis_memory.h include/ferrum/aegis/aegis_types.h | build
	$(CC) $(CFLAGS) tests/aegis/aegis_memory_tests.c src/aegis/aegis_memory.c -o $@ $(LDFLAGS)

build/aegis_decode_tests: tests/aegis/aegis_decode_tests.c src/aegis/aegis_decode.c include/ferrum/aegis/aegis_decode.h include/ferrum/aegis/aegis_types.h | build
	$(CC) $(CFLAGS) tests/aegis/aegis_decode_tests.c src/aegis/aegis_decode.c -o $@ $(LDFLAGS)

AEGIS_ARITH_SRC := $(wildcard src/aegis/ops/aegis_ops_arith*.c) $(wildcard src/aegis/ops/aegis_ops_compare*.c) $(wildcard src/aegis/ops/aegis_ops_convert*.c)
build/aegis_ops_arith_tests: tests/aegis/aegis_ops_arith_tests.c $(AEGIS_ARITH_SRC) | build
	$(CC) $(CFLAGS) tests/aegis/aegis_ops_arith_tests.c $(AEGIS_ARITH_SRC) -o $@ $(LDFLAGS)

AEGIS_FLOW_SRC := $(wildcard src/aegis/ops/aegis_ops_flow*.c) src/aegis/aegis_memory.c
build/aegis_ops_flow_tests: tests/aegis/aegis_ops_flow_tests.c $(AEGIS_FLOW_SRC) | build
	$(CC) $(CFLAGS) tests/aegis/aegis_ops_flow_tests.c $(AEGIS_FLOW_SRC) -o $@ $(LDFLAGS)

build/aegis_ops_data_tests: tests/aegis/aegis_ops_data_tests.c src/aegis/ops/aegis_ops_data.c | build
	$(CC) $(CFLAGS) tests/aegis/aegis_ops_data_tests.c src/aegis/ops/aegis_ops_data.c -o $@ $(LDFLAGS)

AEGIS_MATH_SRC := $(wildcard src/aegis/ops/aegis_ops_vec3*.c) src/aegis/ops/aegis_ops_quat.c
build/aegis_ops_math_tests: tests/aegis/aegis_ops_math_tests.c $(AEGIS_MATH_SRC) | build
	$(CC) $(CFLAGS) tests/aegis/aegis_ops_math_tests.c $(AEGIS_MATH_SRC) -o $@ $(LDFLAGS)

AEGIS_VM_SRC := src/aegis/aegis_vm_init.c src/aegis/aegis_yield.c src/aegis/aegis_memory.c
build/aegis_yield_tests: tests/aegis/aegis_yield_tests.c $(AEGIS_VM_SRC) | build
	$(CC) $(CFLAGS) tests/aegis/aegis_yield_tests.c $(AEGIS_VM_SRC) -o $@ $(LDFLAGS)

AEGIS_ALL_SRC := $(AEGIS_VM_SRC) src/aegis/aegis_vm_run.c src/aegis/aegis_decode.c \
	$(wildcard src/aegis/ops/*.c)
build/aegis_vm_tests: tests/aegis/aegis_vm_tests.c $(AEGIS_ALL_SRC) | build
	$(CC) $(CFLAGS) tests/aegis/aegis_vm_tests.c $(AEGIS_ALL_SRC) -o $@ $(LDFLAGS)

build/aegis_vm_math_stress_tests: tests/aegis/aegis_vm_math_stress_tests.c $(AEGIS_ALL_SRC) | build
	$(CC) $(CFLAGS) tests/aegis/aegis_vm_math_stress_tests.c $(AEGIS_ALL_SRC) -o $@ $(LDFLAGS)

build/aegis_vm_memory_exhaust_tests: tests/aegis/aegis_vm_memory_exhaust_tests.c $(AEGIS_ALL_SRC) | build
	$(CC) $(CFLAGS) tests/aegis/aegis_vm_memory_exhaust_tests.c $(AEGIS_ALL_SRC) -o $@ $(LDFLAGS)

build/aegis_vm_interrupt_tests: tests/aegis/aegis_vm_interrupt_tests.c $(AEGIS_ALL_SRC) | build
	$(CC) $(CFLAGS) tests/aegis/aegis_vm_interrupt_tests.c $(AEGIS_ALL_SRC) -o $@ $(LDFLAGS)

AEGIS_EVENT_SRC := src/aegis/aegis_event_queue.c src/aegis/aegis_topic_table.c \
	src/aegis/aegis_topic_route.c
build/aegis_event_tests: tests/aegis/aegis_event_tests.c $(AEGIS_EVENT_SRC) | build
	$(CC) $(CFLAGS) tests/aegis/aegis_event_tests.c $(AEGIS_EVENT_SRC) -o $@ $(LDFLAGS)

build/aegis_ops_event_tests: tests/aegis/aegis_ops_event_tests.c $(AEGIS_ALL_SRC) $(AEGIS_EVENT_SRC) | build
	$(CC) $(CFLAGS) tests/aegis/aegis_ops_event_tests.c $(AEGIS_ALL_SRC) $(AEGIS_EVENT_SRC) -o $@ $(LDFLAGS)

AEGIS_ASM_SRC := src/aegis/aegis_asm_parse.c src/aegis/aegis_asm_compile.c
build/aegis_asm_tests: tests/aegis/aegis_asm_tests.c $(AEGIS_ASM_SRC) $(AEGIS_ALL_SRC) $(AEGIS_EVENT_SRC) | build
	$(CC) $(CFLAGS) tests/aegis/aegis_asm_tests.c $(AEGIS_ASM_SRC) $(AEGIS_ALL_SRC) $(AEGIS_EVENT_SRC) -o $@ $(LDFLAGS)

AEGIS_RUNTIME_SRC := src/aegis/aegis_runtime_init.c src/aegis/aegis_runtime_load.c src/aegis/aegis_runtime_tick.c
build/aegis_runtime_tests: build/libheadless.a tests/aegis/aegis_runtime_tests.c $(AEGIS_ASM_SRC) $(AEGIS_ALL_SRC) $(AEGIS_EVENT_SRC) $(AEGIS_RUNTIME_SRC) | build
	$(CC) $(CFLAGS) tests/aegis/aegis_runtime_tests.c $(AEGIS_ASM_SRC) $(AEGIS_ALL_SRC) $(AEGIS_EVENT_SRC) $(AEGIS_RUNTIME_SRC) build/libheadless.a -o $@ $(LDFLAGS)

build/p011_renderer_correction_debug_lines_tests: build/liball.a tests/p011_renderer_correction_debug_lines_tests.c | build
	$(CC) $(CFLAGS) tests/p011_renderer_correction_debug_lines_tests.c build/liball.a -o $@ $(LDFLAGS)

# RED tests (may not compile until quantization module exists)
build/p007_net_quantization_determinism_tests: build/libheadless.a tests/p007_net_quantization_determinism_tests.c | build
	$(CC) $(CFLAGS) tests/p007_net_quantization_determinism_tests.c build/libheadless.a -o $@ $(LDFLAGS)
.PHONY: test_red

# RED tests (may not compile until replication protocol exists)
build/p008_net_replication_protocol_tests: build/libheadless.a tests/p008_net_replication_protocol_tests.c | build
	$(CC) $(CFLAGS) tests/p008_net_replication_protocol_tests.c build/libheadless.a -o $@ $(LDFLAGS)
.PHONY: test_red_p008

# Note: this test currently depends on reliable ordered channel implementation.
build/p007_net_reliable_ordered_tests: build/libheadless.a tests/p007_net_reliable_ordered_tests.c | build
	$(CC) $(CFLAGS) tests/p007_net_reliable_ordered_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p004_tests: build/liball.a tests/p004_renderer_gl_loader_tests.c | build
	$(CC) $(CFLAGS) tests/p004_renderer_gl_loader_tests.c build/liball.a -o $@ $(LDFLAGS)

build/p004_shader_tests: build/libheadless.a tests/p004_renderer_shader_tests.c | build
	$(CC) $(CFLAGS) $(RENDERER_TEST_CFLAGS) tests/p004_renderer_shader_tests.c \
$(SRC_ALL) -o $@ $(LDFLAGS) $(RENDERER_TEST_LIBS)

build/p004_buffer_tests: build/libheadless.a tests/p004_renderer_buffer_tests.c | build
	$(CC) $(CFLAGS) $(RENDERER_TEST_CFLAGS) tests/p004_renderer_buffer_tests.c \
$(SRC_ALL) -o $@ $(LDFLAGS) $(RENDERER_TEST_LIBS)

build/p004_uniform_tests: build/libheadless.a tests/p004_renderer_uniform_tests.c | build
	$(CC) $(CFLAGS) $(RENDERER_TEST_CFLAGS) tests/p004_renderer_uniform_tests.c \
$(SRC_ALL) -o $@ $(LDFLAGS) $(RENDERER_TEST_LIBS)

build/p004_palette_tests: build/libheadless.a tests/p004_renderer_palette_tests.c | build
	$(CC) $(CFLAGS) $(RENDERER_TEST_CFLAGS) tests/p004_renderer_palette_tests.c \
$(SRC_ALL) -o $@ $(LDFLAGS) $(RENDERER_TEST_LIBS)

build/p004_pipeline_tests: build/liball.a tests/p004_renderer_pipeline_tests.c | build
	$(CC) $(CFLAGS) tests/p004_renderer_pipeline_tests.c build/liball.a -o $@ $(LDFLAGS)

build/p004_skinning_tests: build/libheadless.a tests/p004_renderer_skinning_tests.c | build
	$(CC) $(CFLAGS) $(RENDERER_TEST_CFLAGS) tests/p004_renderer_skinning_tests.c \
$(SRC_ALL) -o $@ $(LDFLAGS) $(RENDERER_TEST_LIBS)

build/p004_ecs_skinning_tests: build/libheadless.a tests/p004_renderer_ecs_skinning_tests.c | build
	$(CC) $(CFLAGS) $(RENDERER_TEST_CFLAGS) tests/p004_renderer_ecs_skinning_tests.c \
$(SRC_ALL) -o $@ $(LDFLAGS) $(RENDERER_TEST_LIBS)

build/p004_skinning_alloc_tests: build/liball.a tests/p004_renderer_skinning_alloc_tests.c | build
	$(CC) $(CFLAGS) tests/p004_renderer_skinning_alloc_tests.c build/liball.a -o $@ $(LDFLAGS)

build/p004_pipeline_resource_tests: build/libheadless.a tests/p004_renderer_pipeline_resource_tests.c | build
	$(CC) $(CFLAGS) $(RENDERER_TEST_CFLAGS) tests/p004_renderer_pipeline_resource_tests.c \
$(SRC_ALL) -o $@ $(LDFLAGS) $(RENDERER_TEST_LIBS)

build/p004_pipeline_graph_tests: build/libheadless.a tests/p004_renderer_pipeline_graph_tests.c | build
	$(CC) $(CFLAGS) $(RENDERER_TEST_CFLAGS) tests/p004_renderer_pipeline_graph_tests.c \
$(SRC_ALL) -o $@ $(LDFLAGS) $(RENDERER_TEST_LIBS)
build:
	@mkdir -p build


test: $(BIN_HEADLESS) build/p008_net_replication_protocol_tests build/p000_job_queue_sharding_tests build/p000_job_queue_diagnostics_tests build/p000_ws_deque_tests build/p007_net_client_rx_tests build/p007_net_client_rx_udp_topic_tests build/p007_net_topic_dispatch_tests
	./build/p000_tests && ./build/p001_tests && ./build/p002_tests && ./build/p002_memory_apool_tests && ./build/p003_tests \
&& ./build/p007_net_tests && ./build/p007_net_header_tests && ./build/p007_net_ack_tests \
&& ./build/p007_net_unreliable_tests && ./build/p007_net_reliable_tests && ./build/p007_net_test_client_api_tests \
&& ./build/p007_net_test_transport_tests && ./build/p007_net_body_state_newest_wins_tests && ./build/p007_net_rudp_fragmentation_tests \
	&& ./build/p012_net_rudp_reliability_boundary_tests \
	&& ./build/p013_net_rudp_reliability_layer_tests \
	&& ./build/p014_net_rudp_reliability_send_layer_tests \
	&& ./build/p015_server_net_inbound_message_tests \
	&& ./build/p007_net_stream_api_tests \
	&& ./build/p007_net_stream_flush_send_tests \
	&& ./build/p007_net_stream_channel_topic_tests \
	&& ./build/p008_server_client_fiber_stream_tests \
	&& ./build/p008_server_net_runtime_fiber_tests \
	&& ./build/p008_server_entity_net_pump_tests \
	&& ./build/p008_server_body_state_broadcast_tests \
	&& ./build/p008_net_join_spawn_integration_tests \
	&& ./build/p008_net_rudp_loss_convergence_tests \
	&& ./build/p008_net_replication_protocol_tests \
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
	&& ./build/p078_physics_occlusion_demotion_tests \
	&& ./build/p080_physics_phase4_integration_tests \
	&& ./build/p083_physics_position_projection_tests \
	&& ./build/p084_physics_raycast_tests \
	&& ./build/p085_physics_overlap_tests \
	&& ./build/p086_physics_closest_point_tests \
	&& ./build/p087_physics_phase5_integration_tests \
	&& ./build/p088_physics_static_bvh_build_tests \
	&& ./build/p089_physics_static_bvh_query_tests \
	&& ./build/p090_physics_static_bvh_rebuild_tests \
	&& ./build/p091_physics_phase6_integration_tests \
	&& ./build/p092_server_pre_physics_sync_tests \
	&& ./build/p093_island_tier_promote_tests \
	&& ./build/p094_xpbd_dispatch_tests \
	&& ./build/p095_constraint_color_tests \
	&& ./build/p096_tgs_coloring_tests \
	&& ./build/p097_speculative_contact_tests \
	&& ./build/p098_island_split_tests \
	&& ./build/p099_physics_joint_tests \
	&& ./build/p100_physics_joint_constraint_tests \
	&& ./build/p101_physics_joint_island_tests \
	&& ./build/p102_physics_joint_integration_tests \
	&& ./build/p103_net_emulator_tests \
	&& ./build/p104_engine_settings_tests \
	&& ./build/p105_variable_dt_tests \
	&& ./build/p106_video_capture_frame_ring_tests \
	&& ./build/p107_mesh_collider_bvh_tests \
	&& ./build/p108_mesh_narrowphase_tests \
	&& ./build/p109_mesh_integration_tests \
	&& ./build/p110_obj_loader_tests \
	&& ./build/p111_ccd_tests \
	&& ./build/p112_halfspace_tests \
	&& ./build/p007_net_schema_registry_tests \
	&& ./build/p007_net_udp_socket_tests \
	&& ./build/p007_net_rtt_retransmit_tests \
	&& ./build/p007_net_ghost_table_tests \
	&& ./build/p007_net_snapshot_delta_tests \
	&& ./build/p007_net_snapshot_chunk_tests \
	&& ./build/p007_net_interest_tests \
	&& ./build/p007_net_time_sync_tests \
	&& ./build/p007_net_prediction_tests \
	&& ./build/p007_net_validation_tests \
	&& ./build/p007_net_integration_tests \
	&& ./build/p008_pose_interpolator_tests \
	&& ./build/p119_snapshot_interp_tests \
	&& ./build/p009_server_state_update_queue_tests \
	&& ./build/p000_job_queue_diagnostics_tests \
	&& ./build/p000_ws_deque_tests \
	&& ./build/p007_net_client_rx_tests \
	&& ./build/p007_net_client_tx_tests \
	&& ./build/p007_net_client_rx_udp_topic_tests \
	&& ./build/p007_net_topic_dispatch_tests \
	&& ./build/p008_server_tick_loop_tests \
	&& ./build/p008_server_tick_encoder_tests \
	&& ./build/p008_server_loop_integration_tests \
	&& ./build/p011_renderer_correction_debug_lines_tests \
	&& ( [ "$(TRACY)" != "1" ] || ./build/p010_tracy_alloc_override_tests ) \
	&& ./build/entity_attrs_tests \
	&& ./build/edit_script_env_tests \
	&& ./build/aegis_types_tests \
	&& ./build/aegis_memory_tests \
	&& ./build/aegis_decode_tests \
	&& ./build/aegis_ops_arith_tests \
	&& ./build/aegis_ops_flow_tests \
	&& ./build/aegis_ops_data_tests \
	&& ./build/aegis_ops_math_tests \
	&& ./build/aegis_yield_tests \
	&& ./build/aegis_vm_tests \
	&& ./build/aegis_vm_math_stress_tests \
	&& ./build/aegis_vm_memory_exhaust_tests \
	&& ./build/aegis_vm_interrupt_tests \
	&& ./build/aegis_event_tests \
	&& ./build/aegis_ops_event_tests \
	&& ./build/aegis_asm_tests \
	&& ./build/aegis_runtime_tests

TEST_TIMEOUT ?= 20

.PHONY: test_timeout
test_timeout: $(BIN_HEADLESS) build/p000_job_queue_sharding_tests build/p000_job_queue_diagnostics_tests build/p000_ws_deque_tests build/p007_net_client_rx_tests build/p007_net_client_tx_tests build/p007_net_client_rx_udp_topic_tests build/p007_net_topic_dispatch_tests
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
		./build/p007_net_test_client_api_tests \
		./build/p007_net_test_transport_tests \
		./build/p007_net_body_state_newest_wins_tests \
		./build/p007_net_rudp_fragmentation_tests \
		./build/p012_net_rudp_reliability_boundary_tests \
		./build/p013_net_rudp_reliability_layer_tests \
		./build/p014_net_rudp_reliability_send_layer_tests \
		./build/p015_server_net_inbound_message_tests \
		./build/p007_net_schema_registry_tests \
		./build/p007_net_udp_socket_tests \
		./build/p008_pose_interpolator_tests \
		./build/p119_snapshot_interp_tests \
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

.PHONY: perf_phys5
perf_phys5: build/p087_physics_phase5_benchmarks
	./build/p087_physics_phase5_benchmarks

.PHONY: perf_phys6
perf_phys6: build/p088_physics_static_bvh_build_benchmarks build/p091_physics_phase6_benchmarks
	./build/p088_physics_static_bvh_build_benchmarks && ./build/p091_physics_phase6_benchmarks

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

build/demo_server: build/libheadless.a tests/examples/demo_server.c | build
	$(CC) $(CFLAGS) tests/examples/demo_server.c build/libheadless.a -o $@ $(LDFLAGS)

demo_server:
	@$(MAKE) build/demo_server
	@echo "Built: build/demo_server"
	@echo "Usage: ./build/demo_server <port> [duration_s] [--net-workers N] [--phys-workers N]"

build/demo_client: build/liball.a tests/examples/demo_client.c | build
	$(CC) $(CFLAGS) $(RENDERER_TEST_CFLAGS) tests/examples/demo_client.c build/liball.a -o $@ $(LDFLAGS) $(RENDERER_TEST_LIBS)

demo_client:
	@$(MAKE) build/demo_client
	@echo "Built: build/demo_client"
	@echo "Usage: ./build/demo_client <server_ip> <port> [duration_s] [--headless]"

build/editor_tui: build/libheadless.a tools/editor_tui.c | build
	$(CC) $(CFLAGS) tools/editor_tui.c build/libheadless.a -o $@ $(LDFLAGS)

editor_tui:
	@$(MAKE) build/editor_tui
	@echo "Built: build/editor_tui"
	@echo "Usage: ./build/editor_tui [host:port] [--exec <script>]"

clean:
	$(RM) $(BIN) build/libheadless.a build/liball.a build/demo_server build/demo_client build/editor_tui
	$(RM) -r build/obj
