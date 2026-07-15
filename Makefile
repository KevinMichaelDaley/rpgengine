CC  = /usr/bin/gcc
CXX = /usr/bin/g++
JOB_INSTRUMENTATION ?= 0
TRACY ?= 0
STACK_CANARY ?= 0
EMU ?= 0

CFLAGS ?= -std=c11 -Wall -Wextra -pthread -Iinclude -Ithird_party/stb -Ithird_party/glad/include -Iextern/cgltf -Iextern/clay -Iextern/faiss -O3
CFLAGS += -DFR_JOB_INSTRUMENTATION=$(JOB_INSTRUMENTATION)
CFLAGS += -DJOB_STACK_CANARY=$(STACK_CANARY)

ifeq ($(EMU),1)
	CFLAGS += -DFR_NET_EMULATION
endif

LDFLAGS ?= -lm -fopenmp
LDFLAGS += -Lextern/faiss/build/faiss -Lextern/faiss/build/c_api -lfaiss_c -lfaiss -lopenblas -lstdc++
ifeq ($(FAISS_STUB),1)
# Stub build: no FAISS libraries needed
LDFLAGS := -lm -lpthread
endif

# SymX (symbolic differentiation + JIT compilation)
SYMX_DIR  := extern/SymX
SYMX_INC  := -I$(SYMX_DIR)/symx/include -I$(SYMX_DIR)/symx/src
SYMX_INC  += -I$(SYMX_DIR)/symx/extern/Eigen
SYMX_INC  += -I$(SYMX_DIR)/symx/extern/fmt/include
SYMX_INC  += -I$(SYMX_DIR)/symx/extern/BlockedSparseMatrix/include
SYMX_INC  += -I$(SYMX_DIR)/symx/extern/picoSHA2/include
SYMX_DEF  := -DSYMX_CODEGEN_DIR='"/tmp/srd_codegen"'
SYMX_DEF  += -DSYMX_HESS_STORAGE_FLOAT=double
SYMX_DEF  += -DSYMX_COMPILER_PATH='"AUTO"'
SYMX_DEF  += -DSYMX_ENABLE_AVX2 -DBSM_ENABLE_AVX2
SYMX_LIB  := $(SYMX_DIR)/build/symx/libsymx.a
SYMX_FMT  := $(SYMX_DIR)/build/symx/extern/fmt/libfmt.a
SYMX_FLAGS:= -std=c++17 -mavx2 -mfma -O2 $(SYMX_INC) $(SYMX_DEF)
SYMX_LIBS := $(SYMX_LIB) $(SYMX_FMT) -ldl -fopenmp

# Build SymX static library via its own CMake
$(SYMX_LIB) $(SYMX_FMT):
	@echo "Building SymX..."
	mkdir -p $(SYMX_DIR)/build
	cd $(SYMX_DIR)/build && cmake .. -DSYMX_ENABLE_AVX2=ON -DCMAKE_BUILD_TYPE=Release 2>&1 | tail -1
	cd $(SYMX_DIR)/build && $(MAKE) symx -j$$(nproc) 2>&1 | tail -1
	cd $(SYMX_DIR)/build && $(MAKE) fmt -j$$(nproc) 2>&1 | tail -1

.PHONY: symx
symx: $(SYMX_LIB)

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
MATH_SRC := src/math/vec3.c src/math/quat.c $(filter-out src/math/vec3.c src/math/quat.c, $(wildcard src/math/*.c))
MEM_SRC_BASE := $(wildcard src/memory/*.c)
MEM_TRACY_WRAP_SRC := $(wildcard src/memory/alloc_tracy/*.c)
MEM_SRC := $(MEM_SRC_BASE)
ifeq ($(TRACY),1)
	MEM_SRC += $(MEM_TRACY_WRAP_SRC)
endif
ECS_SRC := $(wildcard src/ecs/*.c)
ENTITY_SRC := $(wildcard src/entity/*.c)
RENDERER_SRC := $(wildcard src/renderer/*.c) $(wildcard src/renderer/skinning/*.c) $(wildcard src/renderer/mesh/*.c) $(wildcard src/renderer/draw/*.c) $(wildcard src/renderer/ubo/*.c) $(wildcard src/renderer/gltf/*.c) $(wildcard src/renderer/scene/*.c) $(wildcard src/renderer/resource/*.c)
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
ASSET_SRC := $(wildcard src/asset/*.c)
AEGIS_SRC := $(wildcard src/aegis/*.c) $(wildcard src/aegis/ops/*.c)
LLM_SRC := $(wildcard src/llm/*/*.c) $(wildcard src/llm/*/*/*.c)
ANIM_SRC := $(wildcard src/animation/*.c) $(wildcard src/animation/*/*.c) $(wildcard src/animation/*/*/*.c)
NPC_SRC := $(wildcard src/npc/graph/*.c) $(wildcard src/npc/nav/*.c) $(wildcard src/npc/sense/*.c) $(wildcard src/npc/trade/*.c) $(wildcard src/npc/state/*.c) $(wildcard src/npc/demo/*.c) $(wildcard src/npc/audio/*.c)
PROCGEN_SRC := $(wildcard src/procgen/*.c) $(wildcard src/procgen/grammars/*.c) $(wildcard src/procgen/architect/*.c) $(wildcard src/procgen/critic/*.c) $(wildcard src/procgen/nitrogen/*.c)
LIGHTMAP_SRC := $(wildcard src/lightmap/*.c) $(wildcard src/lightmap/*/*.c)
SRC_HEADLESS := $(JOB_SRC) $(MATH_SRC) $(MEM_SRC) $(ECS_SRC) $(ENTITY_SRC) $(NET_SRC) $(SERVER_SRC) $(PHYS_SRC) $(MESH_SRC) $(ENGINE_SRC) $(EDITOR_SRC) $(ASSET_SRC) $(AEGIS_SRC) $(LLM_SRC) $(ANIM_SRC) $(NPC_SRC) $(PROCGEN_SRC) $(LIGHTMAP_SRC) $(RENDERER_DEBUG_LINES_SRC)

NPC_FAISS_SRC := src/npc/graph/npc_kg_faiss_wrapper.cpp
# Use stub if FAISS is unavailable (FAISS_STUB=1)
ifeq ($(FAISS_STUB),1)
NPC_FAISS_SRC := src/npc/graph/npc_kg_faiss_stub.cpp
endif
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
OBJ_NPC_FAISS := $(OBJDIR)/src/npc/graph/npc_kg_faiss_wrapper.o
ifeq ($(FAISS_STUB),1)
OBJ_NPC_FAISS := $(OBJDIR)/src/npc/graph/npc_kg_faiss_stub.o
endif
OBJ_ALL      := $(OBJ_HEADLESS) $(OBJ_RENDERER) $(OBJ_GLAD) $(OBJ_NPC_FAISS)

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
	build/p120_static_bvh_raycast_tests \
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
	build/p121_ccd_dynamic_tests \
	build/p122_joint_properties_tests \
	build/p123_muscle_activation_tests \
	build/p124_muscle_force_curve_tests \
	build/p125_muscle_tendon_tests \
	build/p126_muscle_geometry_tests \
	build/p127_muscle_unit_tests \
	build/p128_muscle_pair_tests \
	build/p008_server_tick_loop_tests \
	build/p008_server_tick_encoder_tests \
	build/p008_server_loop_integration_tests

ifeq ($(TRACY),1)
BIN_HEADLESS += build/p010_tracy_alloc_override_tests
endif


BIN_HEADLESS += build/entity_attrs_tests
BIN_HEADLESS += build/edit_script_env_tests
BIN_HEADLESS += build/edit_script_rebase_tests
BIN_HEADLESS += build/entity_hide_tests
BIN_HEADLESS += build/viewport_bsp_tests
BIN_HEADLESS += build/nav_mode_tests
BIN_HEADLESS += build/shading_mode_tests
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
BIN_HEADLESS += build/aegis_ops_entity_tests
BIN_HEADLESS += build/aegis_ops_update_tests
BIN_HEADLESS += build/aegis_async_buffer_tests
BIN_HEADLESS += build/aegis_ops_async_tests
BIN_HEADLESS += build/aegis_async_execute_tests
BIN_HEADLESS += build/aegis_llm_prompt_tests
BIN_HEADLESS += build/aegis_sense_tests
BIN_HEADLESS += build/llm_cost_tracker_tests
BIN_HEADLESS += build/aegis_tools_tests
BIN_HEADLESS += build/aegis_ops_signal_tests
BIN_HEADLESS += build/aegis_ops_await_tests
BIN_HEADLESS += build/aegis_runtime_idle_tests
BIN_HEADLESS += build/aegis_signal_integration_tests
BIN_HEADLESS += build/phys_pair_set_tests
BIN_HEADLESS += build/phys_contact_begin_tests
BIN_HEADLESS += build/phys_overlap_begin_tests
BIN_HEADLESS += build/collision_event_integration_tests
BIN_HEADLESS += build/turret_script_e2e_tests
BIN_HEADLESS += build/undo_apply_tests
BIN_HEADLESS += build/undo_conflict_tests
BIN_HEADLESS += build/undo_rebase_tests
BIN_HEADLESS += build/scene_tree_tests
BIN_HEADLESS += build/skeleton_builder_tests
BIN_HEADLESS += build/inline_field_tests
BIN_HEADLESS += build/npc_trade_state_tests
BIN_HEADLESS += build/npc_demo_integration_tests
BIN_HEADLESS += build/npc_state_tests
BIN_HEADLESS += build/npc_scent_tests
BIN_HEADLESS += build/procgen_types_tests
BIN_HEADLESS += build/procgen_tokenize_tests
BIN_HEADLESS += build/procgen_integration_tests
BIN_HEADLESS += build/procgen_grammar_blockout_tests
BIN_HEADLESS += build/procgen_serialize_tests
BIN_HEADLESS += build/procgen_smoke_tests
BIN_HEADLESS += build/procgen_grammar_registry_tests
BIN_HEADLESS += build/procgen_e2e_tests
BIN_HEADLESS += build/procgen_svo_tests
BIN_HEADLESS += build/lm_visibility_tests
BIN_HEADLESS += build/lm_sh_tests
BIN_HEADLESS += build/lm_kdtree_tests
BIN_HEADLESS += build/lm_lightmap_tests
BIN_HEADLESS += build/lm_light_tests
BIN_HEADLESS += build/lm_material_tests
BIN_HEADLESS += build/lm_atlas_tests
BIN_HEADLESS += build/lm_direct_tests
BIN_HEADLESS += build/lm_indirect_tests
BIN_HEADLESS += build/lm_solve_tests
BIN_HEADLESS += build/lm_svo_material_tests
BIN_HEADLESS += build/lm_farfield_tests
BIN_HEADLESS += build/lm_sky_tests
BIN_HEADLESS += build/lm_svo_mip_tests
BIN_HEADLESS += build/lm_svo_voxelize_tests
BIN_HEADLESS += build/obj_mesh_load_tests
BIN_HEADLESS += build/dmesh_load_tests
BIN_HEADLESS += build/lm_mesh_luxel_tests
BIN_HEADLESS += build/lm_mesh_bake_tests
BIN_HEADLESS += build/lm_lightmap_file_tests
BIN_HEADLESS += build/npc_audio_propagation_tests

BIN_RENDERER_TESTS := build/p004_tests build/p004_shader_tests build/p004_texture_tests build/p004_material_tests build/p004_pbr_shader_tests build/p004_light_store_tests build/p004_scene_tests build/p004_depth_prepass_tests build/p004_cluster_tests build/p004_buffer_tests build/p004_gpu_cmd_queue_tests build/p004_gpu_registry_tests build/p004_shadow_slotmap_tests \
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

# C++ rule for FAISS wrapper.
$(OBJDIR)/src/npc/graph/npc_kg_faiss_stub.o: src/npc/graph/npc_kg_faiss_stub.cpp | build
	@mkdir -p $(dir $@)
	$(CXX) $(CFLAGS) -std=c++17 -c $< -o $@

$(OBJDIR)/src/npc/graph/npc_kg_faiss_wrapper.o: src/npc/graph/npc_kg_faiss_wrapper.cpp | build
	@mkdir -p $(dir $@)
	$(CXX) $(CFLAGS) -std=c++17 -c $< -o $@

# Renderer sources need SDL2 flags.
$(patsubst %.c,$(OBJDIR)/%.o,$(RENDERER_SRC)): $(OBJDIR)/%.o: %.c | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(RENDERER_TEST_CFLAGS) -I/usr/include/aarch64-linux-gnu/SDL2 $(DEPFLAGS) -c $< -o $@

# Editor sources also need SDL2 flags.
$(patsubst %.c,$(OBJDIR)/%.o,$(EDITOR_SRC)): $(OBJDIR)/%.o: %.c | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(SDL2_CFLAGS) -I/usr/include/aarch64-linux-gnu/SDL2 -I/usr/include/SDL2 $(DEPFLAGS) -c $< -o $@

# GLAD loader (third-party, suppress warnings).
$(OBJ_GLAD): third_party/glad/src/glad.c | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -w -c $< -o $@

# Static libraries.
build/libheadless.a: $(OBJ_HEADLESS) $(OBJ_NPC_FAISS) | build
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

build/p120_static_bvh_raycast_tests: build/libheadless.a tests/p120_static_bvh_raycast_tests.c | build
	$(CC) $(CFLAGS) tests/p120_static_bvh_raycast_tests.c build/libheadless.a -o $@ $(LDFLAGS)

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

build/p121_ccd_dynamic_tests: build/libheadless.a tests/p121_ccd_dynamic_tests.c | build
	$(CC) $(CFLAGS) tests/p121_ccd_dynamic_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p122_joint_properties_tests: build/libheadless.a tests/p122_joint_properties_tests.c | build
	$(CC) $(CFLAGS) tests/p122_joint_properties_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p123_muscle_activation_tests: build/libheadless.a tests/p123_muscle_activation_tests.c | build
	$(CC) $(CFLAGS) tests/p123_muscle_activation_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p124_muscle_force_curve_tests: build/libheadless.a tests/p124_muscle_force_curve_tests.c | build
	$(CC) $(CFLAGS) tests/p124_muscle_force_curve_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p125_muscle_tendon_tests: build/libheadless.a tests/p125_muscle_tendon_tests.c | build
	$(CC) $(CFLAGS) tests/p125_muscle_tendon_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p126_muscle_geometry_tests: build/libheadless.a tests/p126_muscle_geometry_tests.c | build
	$(CC) $(CFLAGS) tests/p126_muscle_geometry_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p127_muscle_unit_tests: build/libheadless.a tests/p127_muscle_unit_tests.c | build
	$(CC) $(CFLAGS) tests/p127_muscle_unit_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/p128_muscle_pair_tests: build/libheadless.a tests/p128_muscle_pair_tests.c | build
	$(CC) $(CFLAGS) tests/p128_muscle_pair_tests.c build/libheadless.a -o $@ $(LDFLAGS)

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

build/resource_pipeline: build/liball.a tests/visual/resource_pipeline.c | build
	$(CC) $(CFLAGS) $(RENDERER_TEST_CFLAGS) tests/visual/resource_pipeline.c build/liball.a -o $@ $(LDFLAGS) $(RENDERER_TEST_LIBS)

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

build/edit_script_rebase_tests: tests/editor/edit_script_rebase_tests.c build/libheadless.a | build
	$(CC) $(CFLAGS) tests/editor/edit_script_rebase_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/entity_hide_tests: tests/editor/entity_hide_tests.c build/libheadless.a | build
	$(CC) $(CFLAGS) tests/editor/entity_hide_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/undo_apply_tests: tests/editor/undo_apply_tests.c build/libheadless.a | build
	$(CC) $(CFLAGS) tests/editor/undo_apply_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/undo_conflict_tests: tests/editor/undo_conflict_tests.c build/libheadless.a | build
	$(CC) $(CFLAGS) tests/editor/undo_conflict_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/undo_rebase_tests: tests/editor/undo_rebase_tests.c build/libheadless.a | build
	$(CC) $(CFLAGS) tests/editor/undo_rebase_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/scene_tree_tests: tests/editor/scene_tree_tests.c build/libheadless.a | build
	$(CC) $(CFLAGS) tests/editor/scene_tree_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/skeleton_builder_tests: tests/editor/skeleton_builder_tests.c build/libheadless.a | build
	$(CC) $(CFLAGS) tests/editor/skeleton_builder_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/inline_field_tests: tests/editor/inline_field_tests.c build/libheadless.a | build
	$(CC) $(CFLAGS) tests/editor/inline_field_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/cursor_place_tests: tests/editor/cursor_place_tests.c src/editor/scene/cursor_place.c | build
	$(CC) $(CFLAGS) tests/editor/cursor_place_tests.c src/editor/scene/cursor_place.c -o $@ $(LDFLAGS)

VIEWPORT_BSP_SRC := src/editor/scene/viewport_bsp/viewport_bsp_init.c \
	src/editor/scene/viewport_bsp/viewport_bsp_split.c \
	src/editor/scene/viewport_bsp/viewport_bsp_layout.c \
	src/editor/scene/viewport_bsp/viewport_bsp_hit.c \
	src/editor/scene/viewport_bsp/viewport_bsp_drag.c
build/viewport_bsp_tests: tests/editor/scene/viewport_bsp_tests.c $(VIEWPORT_BSP_SRC) | build
	$(CC) $(CFLAGS) tests/editor/scene/viewport_bsp_tests.c $(VIEWPORT_BSP_SRC) -o $@ $(LDFLAGS)

NAV_MODE_SRC := \
	src/editor/viewport/viewport_camera.c \
	src/editor/viewport/viewport_camera_fly.c \
	src/editor/viewport/viewport_camera_zoom.c \
	src/editor/viewport/viewport_camera_query.c \
	$(wildcard src/math/mat4*.c) \
	$(wildcard src/math/vec3*.c)
build/nav_mode_tests: tests/editor/viewport/nav_mode_tests.c $(NAV_MODE_SRC) | build
	$(CC) $(CFLAGS) tests/editor/viewport/nav_mode_tests.c $(NAV_MODE_SRC) -o $@ $(LDFLAGS)

SHADING_MODE_SRC := \
	src/editor/scene/viewport_bsp/viewport_state_init.c \
	src/editor/viewport/viewport_gizmo.c \
	$(NAV_MODE_SRC)
build/shading_mode_tests: tests/editor/viewport/shading_mode_tests.c $(SHADING_MODE_SRC) | build
	$(CC) $(CFLAGS) tests/editor/viewport/shading_mode_tests.c $(SHADING_MODE_SRC) -o $@ $(LDFLAGS)

build/aegis_types_tests: tests/aegis/test_aegis_types.c include/ferrum/aegis/aegis_types.h include/ferrum/aegis/aegis_bytecode.h include/ferrum/aegis/aegis_config.h | build
	$(CC) $(CFLAGS) tests/aegis/test_aegis_types.c -o $@ $(LDFLAGS)

build/aegis_memory_tests: tests/aegis/aegis_memory_tests.c src/aegis/aegis_memory.c include/ferrum/aegis/aegis_memory.h include/ferrum/aegis/aegis_types.h | build
	$(CC) $(CFLAGS) tests/aegis/aegis_memory_tests.c src/aegis/aegis_memory.c -o $@ $(LDFLAGS)

build/aegis_decode_tests: tests/aegis/aegis_decode_tests.c src/aegis/aegis_decode.c include/ferrum/aegis/aegis_decode.h include/ferrum/aegis/aegis_types.h | build
	$(CC) $(CFLAGS) tests/aegis/aegis_decode_tests.c src/aegis/aegis_decode.c -o $@ $(LDFLAGS)

AEGIS_ARITH_SRC := $(wildcard src/aegis/ops/aegis_ops_arith*.c) $(wildcard src/aegis/ops/aegis_ops_compare*.c) $(wildcard src/aegis/ops/aegis_ops_fcompare*.c) $(wildcard src/aegis/ops/aegis_ops_convert*.c)
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

JSON_PARSE_SRC := src/editor/protocol/json_parse.c src/editor/protocol/json_access.c
AEGIS_KG_SRC := src/npc/graph/npc_kg_init.c src/npc/graph/npc_kg_insert.c src/npc/graph/npc_kg_astar.c
AEGIS_ALL_SRC := $(AEGIS_VM_SRC) src/aegis/aegis_vm_run.c src/aegis/aegis_decode.c \
	$(wildcard src/aegis/ops/*.c) \
	src/aegis/aegis_event_queue.c src/aegis/aegis_topic_table.c src/aegis/aegis_topic_route.c \
	src/engine_settings.c $(JSON_PARSE_SRC) \
	$(wildcard src/npc/trade/*.c) $(AEGIS_KG_SRC)
AEGIS_EXTRA_OBJ := $(OBJ_NPC_FAISS)

build/aegis_vm_tests: tests/aegis/aegis_vm_tests.c $(AEGIS_ALL_SRC) $(AEGIS_ASYNC_BUF_SRC) $(AEGIS_ENTITY_DEPS) $(AEGIS_EXTRA_OBJ) | build
	$(CC) $(CFLAGS) tests/aegis/aegis_vm_tests.c $(AEGIS_ALL_SRC) $(AEGIS_ASYNC_BUF_SRC) $(AEGIS_ENTITY_DEPS) $(AEGIS_EXTRA_OBJ) -o $@ $(LDFLAGS)

build/aegis_vm_math_stress_tests: tests/aegis/aegis_vm_math_stress_tests.c $(AEGIS_ALL_SRC) $(AEGIS_ASYNC_BUF_SRC) $(AEGIS_ENTITY_DEPS) $(AEGIS_EXTRA_OBJ) | build
	$(CC) $(CFLAGS) tests/aegis/aegis_vm_math_stress_tests.c $(AEGIS_ALL_SRC) $(AEGIS_ASYNC_BUF_SRC) $(AEGIS_ENTITY_DEPS) $(AEGIS_EXTRA_OBJ) -o $@ $(LDFLAGS)

build/aegis_vm_memory_exhaust_tests: tests/aegis/aegis_vm_memory_exhaust_tests.c $(AEGIS_ALL_SRC) $(AEGIS_ASYNC_BUF_SRC) $(AEGIS_ENTITY_DEPS) $(AEGIS_EXTRA_OBJ) | build
	$(CC) $(CFLAGS) tests/aegis/aegis_vm_memory_exhaust_tests.c $(AEGIS_ALL_SRC) $(AEGIS_ASYNC_BUF_SRC) $(AEGIS_ENTITY_DEPS) $(AEGIS_EXTRA_OBJ) -o $@ $(LDFLAGS)

build/aegis_vm_interrupt_tests: tests/aegis/aegis_vm_interrupt_tests.c $(AEGIS_ALL_SRC) $(AEGIS_ASYNC_BUF_SRC) $(AEGIS_ENTITY_DEPS) $(AEGIS_EXTRA_OBJ) | build
	$(CC) $(CFLAGS) tests/aegis/aegis_vm_interrupt_tests.c $(AEGIS_ALL_SRC) $(AEGIS_ASYNC_BUF_SRC) $(AEGIS_ENTITY_DEPS) $(AEGIS_EXTRA_OBJ) -o $@ $(LDFLAGS)

AEGIS_EVENT_SRC := src/aegis/aegis_event_queue.c src/aegis/aegis_topic_table.c \
	src/aegis/aegis_topic_route.c
build/aegis_event_tests: tests/aegis/aegis_event_tests.c $(AEGIS_EVENT_SRC) | build
	$(CC) $(CFLAGS) tests/aegis/aegis_event_tests.c $(AEGIS_EVENT_SRC) -o $@ $(LDFLAGS)

build/aegis_ops_event_tests: tests/aegis/aegis_ops_event_tests.c $(AEGIS_ALL_SRC) $(AEGIS_ASYNC_BUF_SRC) $(AEGIS_ENTITY_DEPS) $(AEGIS_EXTRA_OBJ) | build
	$(CC) $(CFLAGS) tests/aegis/aegis_ops_event_tests.c $(AEGIS_ALL_SRC) $(AEGIS_ASYNC_BUF_SRC) $(AEGIS_ENTITY_DEPS) $(AEGIS_EXTRA_OBJ) -o $@ $(LDFLAGS)

AEGIS_ASM_SRC := src/aegis/aegis_asm_parse.c src/aegis/aegis_asm_compile.c
build/aegis_asm_tests: tests/aegis/aegis_asm_tests.c $(AEGIS_ASM_SRC) $(AEGIS_ALL_SRC) $(AEGIS_ASYNC_BUF_SRC) $(AEGIS_ENTITY_DEPS) $(AEGIS_EXTRA_OBJ) | build
	$(CC) $(CFLAGS) tests/aegis/aegis_asm_tests.c $(AEGIS_ASM_SRC) $(AEGIS_ALL_SRC) $(AEGIS_ASYNC_BUF_SRC) $(AEGIS_ENTITY_DEPS) $(AEGIS_EXTRA_OBJ) -o $@ $(LDFLAGS)

AEGIS_RUNTIME_SRC := src/aegis/aegis_runtime_init.c src/aegis/aegis_runtime_load.c src/aegis/aegis_runtime_tick.c src/aegis/aegis_runtime_registry.c src/aegis/aegis_runtime_query.c src/aegis/aegis_runtime_idle.c
build/aegis_runtime_tests: build/libheadless.a tests/aegis/aegis_runtime_tests.c | build
	$(CC) $(CFLAGS) tests/aegis/aegis_runtime_tests.c build/libheadless.a -o $@ $(LDFLAGS)

AEGIS_ENTITY_DEPS := src/entity/entity_attrs.c src/entity/entity_attrs_mutate.c src/entity/entity_attrs_search.c
build/aegis_ops_entity_tests: tests/aegis/aegis_ops_entity_tests.c $(AEGIS_ASM_SRC) $(AEGIS_ALL_SRC) $(AEGIS_ASYNC_BUF_SRC) $(AEGIS_ENTITY_DEPS) $(AEGIS_EXTRA_OBJ) | build
	$(CC) $(CFLAGS) tests/aegis/aegis_ops_entity_tests.c $(AEGIS_ASM_SRC) $(AEGIS_ALL_SRC) $(AEGIS_ASYNC_BUF_SRC) $(AEGIS_ENTITY_DEPS) $(AEGIS_EXTRA_OBJ) -o $@ $(LDFLAGS)

build/aegis_ops_update_tests: tests/aegis/aegis_ops_update_tests.c $(AEGIS_ASM_SRC) $(AEGIS_ALL_SRC) $(AEGIS_ASYNC_BUF_SRC) $(AEGIS_ENTITY_DEPS) $(AEGIS_EXTRA_OBJ) | build
	$(CC) $(CFLAGS) tests/aegis/aegis_ops_update_tests.c $(AEGIS_ASM_SRC) $(AEGIS_ALL_SRC) $(AEGIS_ASYNC_BUF_SRC) $(AEGIS_ENTITY_DEPS) $(AEGIS_EXTRA_OBJ) -o $@ $(LDFLAGS)

AEGIS_ASYNC_BUF_SRC := src/aegis/aegis_async_buffer.c src/aegis/aegis_async_buffer_io.c
AEGIS_ASYNC_SRC := $(AEGIS_ASYNC_BUF_SRC) src/aegis/aegis_async_execute.c
build/aegis_async_buffer_tests: tests/aegis/aegis_async_buffer_tests.c $(AEGIS_ASYNC_BUF_SRC) | build
	$(CC) $(CFLAGS) tests/aegis/aegis_async_buffer_tests.c $(AEGIS_ASYNC_BUF_SRC) -o $@ $(LDFLAGS)

build/aegis_ops_async_tests: build/libheadless.a tests/aegis/aegis_ops_async_tests.c | build
	$(CC) $(CFLAGS) tests/aegis/aegis_ops_async_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/aegis_async_execute_tests: build/libheadless.a tests/aegis/aegis_async_execute_tests.c | build
	$(CC) $(CFLAGS) tests/aegis/aegis_async_execute_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/aegis_llm_prompt_tests: build/libheadless.a tests/aegis/aegis_llm_prompt_tests.c | build
	$(CC) $(CFLAGS) tests/aegis/aegis_llm_prompt_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/aegis_sense_tests: build/libheadless.a tests/aegis/aegis_sense_tests.c | build
	$(CC) $(CFLAGS) tests/aegis/aegis_sense_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/aegis_tools_tests: build/libheadless.a tests/aegis/aegis_tools_tests.c | build
	$(CC) $(CFLAGS) tests/aegis/aegis_tools_tests.c build/libheadless.a -o $@ $(LDFLAGS)

NPC_KG_TEST_SRC := $(wildcard src/npc/graph/*.c)
build/npc_knowledge_graph_tests: tests/npc/npc_knowledge_graph_tests.c $(NPC_KG_TEST_SRC) $(OBJ_NPC_FAISS) | build
	$(CC) $(CFLAGS) tests/npc/npc_knowledge_graph_tests.c $(NPC_KG_TEST_SRC) $(OBJ_NPC_FAISS) -o $@ $(LDFLAGS)

build/npc_kg_astar_tests: tests/npc/npc_kg_astar_tests.c $(NPC_KG_TEST_SRC) $(OBJ_NPC_FAISS) | build
	$(CC) $(CFLAGS) tests/npc/npc_kg_astar_tests.c $(NPC_KG_TEST_SRC) $(OBJ_NPC_FAISS) -o $@ $(LDFLAGS)

build/npc_kg_spatial_tests: tests/npc/npc_kg_spatial_tests.c $(NPC_KG_TEST_SRC) $(OBJ_NPC_FAISS) | build
	$(CC) $(CFLAGS) tests/npc/npc_kg_spatial_tests.c $(NPC_KG_TEST_SRC) $(OBJ_NPC_FAISS) -o $@ $(LDFLAGS)

build/npc_faiss_tests: tests/npc/npc_faiss_tests.c src/npc/graph/npc_kg_faiss_wrapper.cpp | build
	$(CXX) $(CFLAGS) -std=c++17 tests/npc/npc_faiss_tests.c src/npc/graph/npc_kg_faiss_wrapper.cpp -o $@ $(LDFLAGS)

NPC_SVO_TEST_SRC := src/npc/nav/npc_svo_init.c src/npc/nav/npc_svo_rasterize.c src/npc/nav/npc_svo_floodfill.c src/npc/nav/npc_svo_blocker.c src/npc/nav/npc_pathfind_svo_astar.c src/npc/nav/npc_pathfind_graph_astar.c src/npc/nav/npc_pathfind_shortcut.c src/npc/nav/npc_nav_graph_build.c src/npc/nav/npc_nav_graph_reduce.c
build/npc_svo_tests: tests/npc/npc_svo_tests.c $(NPC_SVO_TEST_SRC) | build
	$(CC) $(CFLAGS) tests/npc/npc_svo_tests.c $(NPC_SVO_TEST_SRC) -o $@ $(LDFLAGS)

build/npc_nav_graph_tests: tests/npc/npc_nav_graph_tests.c $(NPC_SVO_TEST_SRC) | build
	$(CC) $(CFLAGS) tests/npc/npc_nav_graph_tests.c $(NPC_SVO_TEST_SRC) -o $@ $(LDFLAGS)

build/npc_pathfind_tests: tests/npc/npc_pathfind_tests.c $(NPC_SVO_TEST_SRC) | build
	$(CC) $(CFLAGS) tests/npc/npc_pathfind_tests.c $(NPC_SVO_TEST_SRC) -o $@ $(LDFLAGS)

build/npc_nav_action_tests: build/libheadless.a tests/npc/npc_nav_action_tests.c | build
	$(CC) $(CFLAGS) tests/npc/npc_nav_action_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/npc_nav_integration_tests: build/libheadless.a tests/npc/npc_nav_integration_tests.c | build
	$(CC) $(CFLAGS) tests/npc/npc_nav_integration_tests.c build/libheadless.a -o $@ $(LDFLAGS)

NPC_TRADE_TEST_SRC := $(wildcard src/npc/trade/*.c)
build/npc_trade_state_tests: tests/npc/npc_trade_state_tests.c $(NPC_TRADE_TEST_SRC) include/ferrum/npc/npc_trade.h | build
	$(CC) $(CFLAGS) tests/npc/npc_trade_state_tests.c $(NPC_TRADE_TEST_SRC) -o $@ $(LDFLAGS)

NPC_STATE_TEST_SRC := $(wildcard src/npc/state/*.c) src/npc/graph/npc_kg_init.c src/npc/graph/npc_kg_insert.c src/npc/sense/npc_sense_auto.c
build/npc_state_tests: tests/npc/npc_state_tests.c $(NPC_STATE_TEST_SRC) $(OBJ_NPC_FAISS) | build
	$(CC) $(CFLAGS) tests/npc/npc_state_tests.c $(NPC_STATE_TEST_SRC) $(OBJ_NPC_FAISS) -o $@ $(LDFLAGS) -lm

build/npc_demo_integration_tests: build/libheadless.a tests/npc/npc_demo_integration_tests.c | build
	$(CC) $(CFLAGS) tests/npc/npc_demo_integration_tests.c build/libheadless.a -o $@ $(LDFLAGS)

NPC_SENSE_TEST_SRC := $(wildcard src/npc/sense/*.c) $(wildcard src/npc/graph/npc_kg_init.c) $(wildcard src/npc/graph/npc_kg_insert.c) $(wildcard src/npc/graph/npc_kg_decay.c)
build/npc_sense_tests: tests/npc/npc_sense_tests.c $(NPC_SENSE_TEST_SRC) $(OBJ_NPC_FAISS) | build
	$(CC) $(CFLAGS) tests/npc/npc_sense_tests.c $(NPC_SENSE_TEST_SRC) $(OBJ_NPC_FAISS) -o $@ $(LDFLAGS)

NPC_SCENT_TEST_SRC := src/npc/sense/npc_sense_scent.c
build/npc_scent_tests: tests/npc/npc_scent_tests.c $(NPC_SCENT_TEST_SRC) | build
	$(CC) $(CFLAGS) tests/npc/npc_scent_tests.c $(NPC_SCENT_TEST_SRC) -o $@ $(LDFLAGS)

build/procgen_types_tests: tests/procgen/procgen_types_tests.c include/ferrum/procgen/procgen_types.h include/ferrum/procgen/procgen_layout.h | build
	$(CC) $(CFLAGS) tests/procgen/procgen_types_tests.c -o $@ -lm

build/procgen_tokenize_tests: tests/procgen/procgen_tokenize_tests.c src/procgen/procgen_tokenize.c include/ferrum/procgen/procgen_tokenize.h include/ferrum/procgen/procgen_types.h include/ferrum/procgen/procgen_layout.h | build
	$(CC) $(CFLAGS) tests/procgen/procgen_tokenize_tests.c src/procgen/procgen_tokenize.c -o $@ -lm

build/procgen_integration_tests: tests/procgen/procgen_integration_tests.c src/procgen/procgen_tokenize.c include/ferrum/procgen/procgen_tokenize.h include/ferrum/procgen/procgen_types.h include/ferrum/procgen/procgen_layout.h | build
	$(CC) $(CFLAGS) tests/procgen/procgen_integration_tests.c src/procgen/procgen_tokenize.c -o $@ -lm

build/procgen_grammar_blockout_tests: tests/procgen/procgen_grammar_blockout_tests.c src/procgen/procgen_tokenize.c src/procgen/grammars/grammar_blockout.c include/ferrum/procgen/procgen_tokenize.h include/ferrum/procgen/procgen_types.h include/ferrum/procgen/procgen_layout.h include/ferrum/procgen/grammar_blockout.h | build
	$(CC) $(CFLAGS) tests/procgen/procgen_grammar_blockout_tests.c src/procgen/procgen_tokenize.c src/procgen/grammars/grammar_blockout.c -o $@ -lm

build/procgen_serialize_tests: tests/procgen/procgen_serialize_tests.c src/procgen/procgen_tokenize.c src/procgen/grammars/grammar_blockout.c src/procgen/procgen_serialize.c include/ferrum/procgen/procgen_serialize.h include/ferrum/procgen/procgen_tokenize.h include/ferrum/procgen/procgen_types.h include/ferrum/procgen/procgen_layout.h include/ferrum/procgen/grammar_blockout.h | build
	$(CC) $(CFLAGS) tests/procgen/procgen_serialize_tests.c src/procgen/procgen_tokenize.c src/procgen/grammars/grammar_blockout.c src/procgen/procgen_serialize.c -o $@ -lm

build/procgen_smoke_tests: tests/procgen/procgen_smoke_tests.c src/procgen/procgen_tokenize.c src/procgen/grammars/grammar_blockout.c src/procgen/procgen_serialize.c include/ferrum/procgen/procgen_serialize.h include/ferrum/procgen/procgen_tokenize.h include/ferrum/procgen/procgen_types.h include/ferrum/procgen/procgen_layout.h include/ferrum/procgen/grammar_blockout.h | build
	$(CC) $(CFLAGS) tests/procgen/procgen_smoke_tests.c src/procgen/procgen_tokenize.c src/procgen/grammars/grammar_blockout.c src/procgen/procgen_serialize.c -o $@ -lm

build/procgen_grammar_registry_tests: tests/procgen/procgen_grammar_registry_tests.c src/procgen/procgen_tokenize.c src/procgen/grammars/grammar_blockout.c src/procgen/procgen_grammar_registry.c include/ferrum/procgen/procgen_grammar_registry.h include/ferrum/procgen/procgen_tokenize.h include/ferrum/procgen/procgen_types.h include/ferrum/procgen/procgen_layout.h include/ferrum/procgen/grammar_blockout.h | build
	$(CC) $(CFLAGS) tests/procgen/procgen_grammar_registry_tests.c src/procgen/procgen_tokenize.c src/procgen/grammars/grammar_blockout.c src/procgen/procgen_grammar_registry.c -o $@ -lm

build/procgen_e2e_tests: tests/procgen/procgen_e2e_tests.c src/procgen/procgen_tokenize.c src/procgen/grammars/grammar_blockout.c src/procgen/procgen_serialize.c src/procgen/procgen_grammar_registry.c include/ferrum/procgen/procgen_serialize.h include/ferrum/procgen/procgen_grammar_registry.h include/ferrum/procgen/procgen_tokenize.h include/ferrum/procgen/procgen_types.h include/ferrum/procgen/procgen_layout.h include/ferrum/procgen/grammar_blockout.h | build
	$(CC) $(CFLAGS) tests/procgen/procgen_e2e_tests.c src/procgen/procgen_tokenize.c src/procgen/grammars/grammar_blockout.c src/procgen/procgen_serialize.c src/procgen/procgen_grammar_registry.c -o $@ -lm

build/procgen_svo_tests: tests/procgen/procgen_svo_tests.c src/procgen/procgen_svo_builder.c src/procgen/procgen_mesh.c src/npc/nav/npc_svo_init.c include/ferrum/procgen/procgen_svo_builder.h include/ferrum/procgen/procgen_layout.h | build
	$(CC) $(CFLAGS) tests/procgen/procgen_svo_tests.c src/procgen/procgen_svo_builder.c src/procgen/procgen_mesh.c src/npc/nav/npc_svo_init.c -o $@ -lm

build/lm_visibility_tests: tests/lightmap/lm_visibility_tests.c src/lightmap/lm_visibility.c src/npc/nav/npc_svo_init.c src/npc/nav/npc_svo_rasterize.c src/npc/nav/npc_svo_blocker.c src/math/vec3.c include/ferrum/lightmap/lm_visibility.h | build
	$(CC) $(CFLAGS) tests/lightmap/lm_visibility_tests.c src/lightmap/lm_visibility.c src/npc/nav/npc_svo_init.c src/npc/nav/npc_svo_rasterize.c src/npc/nav/npc_svo_blocker.c src/math/vec3.c -o $@ -lm

build/lm_sh_tests: tests/lightmap/lm_sh_tests.c src/lightmap/lm_sh.c src/math/vec3.c include/ferrum/lightmap/lm_sh.h | build
	$(CC) $(CFLAGS) tests/lightmap/lm_sh_tests.c src/lightmap/lm_sh.c src/math/vec3.c -o $@ -lm

build/lm_kdtree_tests: tests/lightmap/lm_kdtree_tests.c src/lightmap/lm_kdtree.c src/math/vec3.c src/memory/arena_init.c src/memory/arena_alloc.c src/memory/arena_mark.c src/memory/arena_pop.c include/ferrum/lightmap/lm_kdtree.h | build
	$(CC) $(CFLAGS) tests/lightmap/lm_kdtree_tests.c src/lightmap/lm_kdtree.c src/math/vec3.c src/memory/arena_init.c src/memory/arena_alloc.c src/memory/arena_mark.c src/memory/arena_pop.c -o $@ -lm

LM_CORE_SRC := src/lightmap/lm_lightmap.c src/lightmap/lm_sh.c src/math/vec3.c src/memory/arena_init.c src/memory/arena_alloc.c
build/lm_lightmap_tests: tests/lightmap/lm_lightmap_tests.c $(LM_CORE_SRC) include/ferrum/lightmap/lm_lightmap.h include/ferrum/lightmap/lm_types.h | build
	$(CC) $(CFLAGS) tests/lightmap/lm_lightmap_tests.c $(LM_CORE_SRC) -o $@ -lm

build/lm_light_tests: tests/lightmap/lm_light_tests.c src/lightmap/lm_light.c src/math/vec3.c include/ferrum/lightmap/lm_light.h | build
	$(CC) $(CFLAGS) tests/lightmap/lm_light_tests.c src/lightmap/lm_light.c src/math/vec3.c -o $@ -lm

build/lm_material_tests: tests/lightmap/lm_material_tests.c src/lightmap/lm_material.c include/ferrum/lightmap/lm_material.h | build
	$(CC) $(CFLAGS) tests/lightmap/lm_material_tests.c src/lightmap/lm_material.c -o $@ -lm

build/lm_atlas_tests: tests/lightmap/lm_atlas_tests.c src/lightmap/lm_atlas.c include/ferrum/lightmap/lm_atlas.h | build
	$(CC) $(CFLAGS) tests/lightmap/lm_atlas_tests.c src/lightmap/lm_atlas.c -o $@ -lm

LM_DIRECT_SRC := src/lightmap/lm_direct.c src/lightmap/lm_visibility.c $(LM_CORE_SRC) src/npc/nav/npc_svo_init.c src/npc/nav/npc_svo_rasterize.c src/npc/nav/npc_svo_blocker.c
build/lm_direct_tests: tests/lightmap/lm_direct_tests.c $(LM_DIRECT_SRC) include/ferrum/lightmap/lm_direct.h | build
	$(CC) $(CFLAGS) tests/lightmap/lm_direct_tests.c $(LM_DIRECT_SRC) -o $@ -lm

LM_INDIRECT_SRC := src/lightmap/lm_indirect.c src/lightmap/lm_light.c src/lightmap/lm_visibility.c $(LM_CORE_SRC) src/npc/nav/npc_svo_init.c src/npc/nav/npc_svo_rasterize.c src/npc/nav/npc_svo_blocker.c
build/lm_indirect_tests: tests/lightmap/lm_indirect_tests.c $(LM_INDIRECT_SRC) include/ferrum/lightmap/lm_indirect.h | build
	$(CC) $(CFLAGS) tests/lightmap/lm_indirect_tests.c $(LM_INDIRECT_SRC) -o $@ -lm

LM_SOLVE_SRC := src/lightmap/lm_solve.c src/lightmap/lm_kdtree.c src/memory/arena_mark.c src/memory/arena_pop.c src/lightmap/lm_visibility.c $(LM_CORE_SRC) src/npc/nav/npc_svo_init.c src/npc/nav/npc_svo_rasterize.c src/npc/nav/npc_svo_blocker.c
build/lm_solve_tests: tests/lightmap/lm_solve_tests.c $(LM_SOLVE_SRC) include/ferrum/lightmap/lm_solve.h | build
	$(CC) $(CFLAGS) tests/lightmap/lm_solve_tests.c $(LM_SOLVE_SRC) -o $@ -lm

LM_SVO_MAT_SRC := src/npc/nav/npc_svo_init.c src/npc/nav/npc_svo_rasterize.c src/npc/nav/npc_svo_blocker.c src/math/vec3.c
build/lm_svo_material_tests: tests/lightmap/lm_svo_material_tests.c src/lightmap/lm_svo_material.c $(LM_SVO_MAT_SRC) include/ferrum/lightmap/lm_svo_material.h | build
	$(CC) $(CFLAGS) tests/lightmap/lm_svo_material_tests.c src/lightmap/lm_svo_material.c $(LM_SVO_MAT_SRC) -o $@ -lm

LM_FARFIELD_SRC := src/lightmap/lm_farfield.c src/lightmap/lm_sky.c src/lightmap/lm_svo_mip.c src/lightmap/lm_image.c src/lightmap/lm_material.c src/lightmap/lm_svo_material.c src/lightmap/lm_visibility.c $(LM_CORE_SRC) src/npc/nav/npc_svo_init.c src/npc/nav/npc_svo_rasterize.c src/npc/nav/npc_svo_blocker.c
build/lm_farfield_tests: tests/lightmap/lm_farfield_tests.c $(LM_FARFIELD_SRC) include/ferrum/lightmap/lm_farfield.h | build
	$(CC) $(CFLAGS) tests/lightmap/lm_farfield_tests.c $(LM_FARFIELD_SRC) -o $@ -lm

build/lm_sky_tests: tests/lightmap/lm_sky_tests.c src/lightmap/lm_sky.c src/lightmap/lm_image.c src/math/vec3.c include/ferrum/lightmap/lm_sky.h | build
	$(CC) $(CFLAGS) tests/lightmap/lm_sky_tests.c src/lightmap/lm_sky.c src/lightmap/lm_image.c src/math/vec3.c -o $@ -lm

build/lm_svo_mip_tests: tests/lightmap/lm_svo_mip_tests.c src/lightmap/lm_svo_mip.c src/lightmap/lm_svo_material.c src/lightmap/lm_material.c $(LM_SVO_MAT_SRC) include/ferrum/lightmap/lm_svo_mip.h | build
	$(CC) $(CFLAGS) tests/lightmap/lm_svo_mip_tests.c src/lightmap/lm_svo_mip.c src/lightmap/lm_svo_material.c src/lightmap/lm_material.c $(LM_SVO_MAT_SRC) -o $@ -lm

build/lm_svo_voxelize_tests: tests/lightmap/lm_svo_voxelize_tests.c src/lightmap/lm_svo_voxelize.c src/lightmap/lm_svo_mip.c src/lightmap/lm_svo_material.c src/lightmap/lm_material.c src/lightmap/lm_image.c $(LM_SVO_MAT_SRC) include/ferrum/lightmap/lm_svo_voxelize.h | build
	$(CC) $(CFLAGS) tests/lightmap/lm_svo_voxelize_tests.c src/lightmap/lm_svo_voxelize.c src/lightmap/lm_svo_mip.c src/lightmap/lm_svo_material.c src/lightmap/lm_material.c src/lightmap/lm_image.c $(LM_SVO_MAT_SRC) -o $@ -lm

CFLAGS_CURL := $(shell pkg-config --cflags libcurl 2>/dev/null)
LDFLAGS_CURL := $(shell pkg-config --libs libcurl 2>/dev/null)

build/procgen_architect_cli: tools/procgen_architect_cli.c src/procgen/procgen_tokenize.c src/procgen/grammars/grammar_blockout.c src/procgen/procgen_serialize.c src/procgen/architect/architect.c src/engine_settings.c src/llm/cost/llm_cost_tracker.c src/llm/cost/llm_cost_compute.c include/ferrum/procgen/procgen_architect.h include/ferrum/procgen/procgen_tokenize.h include/ferrum/procgen/procgen_types.h include/ferrum/procgen/procgen_layout.h include/ferrum/procgen/grammar_blockout.h include/ferrum/procgen/procgen_serialize.h include/ferrum/engine_settings.h | build
	$(CC) $(CFLAGS) $(CFLAGS_CURL) tools/procgen_architect_cli.c src/procgen/procgen_tokenize.c src/procgen/grammars/grammar_blockout.c src/procgen/procgen_serialize.c src/procgen/architect/architect.c src/engine_settings.c src/llm/cost/llm_cost_tracker.c src/llm/cost/llm_cost_compute.c -o $@ -lm $(LDFLAGS_CURL)

architect: build/procgen_architect_cli
	@echo "Built: build/procgen_architect_cli"
	@echo "Usage: ./build/procgen_architect_cli \"prompt\" --output /tmp/test.json"

build/procgen_to_obj: tools/procgen_to_obj.c build/libheadless.a include/ferrum/procgen/procgen_level_load.h include/ferrum/procgen/procgen_svo_builder.h | build
	$(CC) $(CFLAGS) tools/procgen_to_obj.c build/libheadless.a -o $@ -lm -lpthread

build/procgen_viewer: tools/procgen_viewer.c | build
	$(CC) $(CFLAGS) $(RENDERER_TEST_CFLAGS) tools/procgen_viewer.c -o $@ $(RENDERER_TEST_LIBS) -lGLU -lm

NPC_AUDIO_PROP_TEST_SRC := $(wildcard src/npc/audio/*.c)
build/npc_audio_propagation_tests: tests/npc/npc_audio_propagation_tests.c $(NPC_AUDIO_PROP_TEST_SRC) | build
	$(CC) $(CFLAGS) tests/npc/npc_audio_propagation_tests.c $(NPC_AUDIO_PROP_TEST_SRC) -o $@ $(LDFLAGS)

build/llm_cost_tracker_tests: tests/llm/llm_cost_tracker_tests.c src/llm/cost/llm_cost_tracker.c src/llm/cost/llm_cost_compute.c | build
	$(CC) $(CFLAGS) tests/llm/llm_cost_tracker_tests.c src/llm/cost/llm_cost_tracker.c src/llm/cost/llm_cost_compute.c -o $@ $(LDFLAGS)

build/llm_smoke_test: tests/llm/llm_smoke_test.c src/engine_settings.c src/llm/cost/llm_cost_tracker.c src/llm/cost/llm_cost_compute.c | build
	$(CC) $(CFLAGS) tests/llm/llm_smoke_test.c src/engine_settings.c src/llm/cost/llm_cost_tracker.c src/llm/cost/llm_cost_compute.c -o $@ $(shell pkg-config --libs libcurl) $(LDFLAGS)

build/aegis_ops_signal_tests: tests/aegis/aegis_ops_signal_tests.c $(AEGIS_ASM_SRC) $(AEGIS_ALL_SRC) $(AEGIS_ASYNC_BUF_SRC) $(AEGIS_ENTITY_DEPS) $(AEGIS_EXTRA_OBJ) | build
	$(CC) $(CFLAGS) tests/aegis/aegis_ops_signal_tests.c $(AEGIS_ASM_SRC) $(AEGIS_ALL_SRC) $(AEGIS_ASYNC_BUF_SRC) $(AEGIS_ENTITY_DEPS) $(AEGIS_EXTRA_OBJ) -o $@ $(LDFLAGS)

build/aegis_ops_await_tests: tests/aegis/aegis_ops_await_tests.c $(AEGIS_ASM_SRC) $(AEGIS_ALL_SRC) $(AEGIS_ASYNC_BUF_SRC) $(AEGIS_ENTITY_DEPS) $(AEGIS_EXTRA_OBJ) | build
	$(CC) $(CFLAGS) tests/aegis/aegis_ops_await_tests.c $(AEGIS_ASM_SRC) $(AEGIS_ALL_SRC) $(AEGIS_ASYNC_BUF_SRC) $(AEGIS_ENTITY_DEPS) $(AEGIS_EXTRA_OBJ) -o $@ $(LDFLAGS)

build/aegis_runtime_idle_tests: build/libheadless.a tests/aegis/aegis_runtime_idle_tests.c | build
	$(CC) $(CFLAGS) tests/aegis/aegis_runtime_idle_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/aegis_signal_integration_tests: build/libheadless.a tests/aegis/aegis_signal_integration_tests.c | build
	$(CC) $(CFLAGS) tests/aegis/aegis_signal_integration_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/turret_script_e2e_tests: build/libheadless.a tests/aegis/turret_script_e2e_tests.c | build
	$(CC) $(CFLAGS) tests/aegis/turret_script_e2e_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/phys_pair_set_tests: build/libheadless.a tests/physics/phys_pair_set_tests.c | build
	$(CC) $(CFLAGS) tests/physics/phys_pair_set_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/phys_contact_begin_tests: build/libheadless.a tests/physics/phys_contact_begin_tests.c | build
	$(CC) $(CFLAGS) tests/physics/phys_contact_begin_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/phys_overlap_begin_tests: build/libheadless.a tests/physics/phys_overlap_begin_tests.c | build
	$(CC) $(CFLAGS) tests/physics/phys_overlap_begin_tests.c build/libheadless.a -o $@ $(LDFLAGS)

build/collision_event_integration_tests: build/libheadless.a tests/physics/collision_event_integration_tests.c | build
	$(CC) $(CFLAGS) tests/physics/collision_event_integration_tests.c build/libheadless.a -o $@ $(LDFLAGS)

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

build/p004_texture_tests: tests/p004_renderer_texture_tests.c src/renderer/texture_create.c src/renderer/texture_upload.c src/renderer/texture_bind.c $(OBJ_GLAD) | build
	$(CC) $(CFLAGS) $(RENDERER_TEST_CFLAGS) tests/p004_renderer_texture_tests.c \
src/renderer/texture_create.c src/renderer/texture_upload.c src/renderer/texture_bind.c \
$(OBJ_GLAD) -o $@ $(RENDERER_TEST_LIBS) -ldl -lm

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
build/p004_static_mesh_tests: build/libheadless.a tests/p004_renderer_static_mesh_tests.c $(OBJ_GLAD) | build
	$(CC) $(CFLAGS) $(RENDERER_TEST_CFLAGS) tests/p004_renderer_static_mesh_tests.c \
$(SRC_ALL) $(OBJ_GLAD) -o $@ $(LDFLAGS) $(RENDERER_TEST_LIBS)
build/p004_skeletal_mesh_tests: build/libheadless.a tests/p004_renderer_skeletal_mesh_tests.c $(OBJ_GLAD) | build
	$(CC) $(CFLAGS) $(RENDERER_TEST_CFLAGS) tests/p004_renderer_skeletal_mesh_tests.c \
$(SRC_ALL) $(OBJ_GLAD) -o $@ $(LDFLAGS) $(RENDERER_TEST_LIBS)
build/p004_mesh_registry_tests: build/libheadless.a tests/p004_renderer_mesh_registry_tests.c $(OBJ_GLAD) | build
	$(CC) $(CFLAGS) $(RENDERER_TEST_CFLAGS) tests/p004_renderer_mesh_registry_tests.c \
$(SRC_ALL) $(OBJ_GLAD) -o $@ $(LDFLAGS) $(RENDERER_TEST_LIBS)
build/p004_draw_list_tests: build/libheadless.a tests/p004_renderer_draw_list_tests.c $(OBJ_GLAD) | build
	$(CC) $(CFLAGS) $(RENDERER_TEST_CFLAGS) tests/p004_renderer_draw_list_tests.c \
$(SRC_ALL) $(OBJ_GLAD) -o $@ $(LDFLAGS) $(RENDERER_TEST_LIBS)
build/p004_pass_pipeline_tests: build/libheadless.a tests/p004_renderer_pass_pipeline_tests.c $(OBJ_GLAD) | build
	$(CC) $(CFLAGS) $(RENDERER_TEST_CFLAGS) tests/p004_renderer_pass_pipeline_tests.c \
$(SRC_ALL) $(OBJ_GLAD) -o $@ $(LDFLAGS) $(RENDERER_TEST_LIBS)
build/p004_ubo_tests: build/libheadless.a tests/p004_renderer_ubo_tests.c $(OBJ_GLAD) | build
	$(CC) $(CFLAGS) $(RENDERER_TEST_CFLAGS) tests/p004_renderer_ubo_tests.c \
$(SRC_ALL) $(OBJ_GLAD) -o $@ $(LDFLAGS) $(RENDERER_TEST_LIBS)
build/p004_visual_mesh_primitives: build/libheadless.a tests/visual/p004_visual_mesh_primitives.c $(OBJ_GLAD) | build
	$(CC) $(CFLAGS) $(RENDERER_TEST_CFLAGS) tests/visual/p004_visual_mesh_primitives.c \
$(SRC_ALL) $(OBJ_GLAD) -o $@ $(LDFLAGS) $(RENDERER_TEST_LIBS)
build/p004_gltf_loader_tests: build/libheadless.a tests/p004_gltf_loader_tests.c $(OBJ_GLAD) | build
	$(CC) $(CFLAGS) $(RENDERER_TEST_CFLAGS) tests/p004_gltf_loader_tests.c \
$(SRC_ALL) $(OBJ_GLAD) -o $@ $(LDFLAGS) $(RENDERER_TEST_LIBS)
build/p004_visual_humanoid: build/libheadless.a tests/visual/p004_visual_humanoid.c $(OBJ_GLAD) | build
	$(CC) $(CFLAGS) $(RENDERER_TEST_CFLAGS) tests/visual/p004_visual_humanoid.c \
$(SRC_ALL) $(OBJ_GLAD) -o $@ $(LDFLAGS) $(RENDERER_TEST_LIBS)
build/p005_visual_ik_reach: build/libheadless.a tests/visual/p005_visual_ik_reach.c $(OBJ_GLAD) | build
	$(CC) $(CFLAGS) $(RENDERER_TEST_CFLAGS) tests/visual/p005_visual_ik_reach.c \
$(SRC_ALL) $(OBJ_GLAD) -o $@ $(LDFLAGS) $(RENDERER_TEST_LIBS)
build/p005_visual_floor_limit: build/libheadless.a tests/visual/p005_visual_floor_limit.c $(OBJ_GLAD) | build
	$(CC) $(CFLAGS) $(RENDERER_TEST_CFLAGS) tests/visual/p005_visual_floor_limit.c \
$(SRC_ALL) $(OBJ_GLAD) -o $@ $(LDFLAGS) $(RENDERER_TEST_LIBS)
build/p005_visual_tracking: build/libheadless.a tests/visual/p005_visual_tracking.c $(OBJ_GLAD) | build
	$(CC) $(CFLAGS) $(RENDERER_TEST_CFLAGS) tests/visual/p005_visual_tracking.c \
$(SRC_ALL) $(OBJ_GLAD) -o $@ $(LDFLAGS) $(RENDERER_TEST_LIBS)
build/p005_visual_walk_cycle: build/libheadless.a tests/visual/p005_visual_walk_cycle.c $(OBJ_GLAD) | build
	$(CC) $(CFLAGS) $(RENDERER_TEST_CFLAGS) tests/visual/p005_visual_walk_cycle.c \
$(SRC_ALL) $(OBJ_GLAD) -o $@ $(LDFLAGS) $(RENDERER_TEST_LIBS)
build/p006_visual_cube_tower: build/libheadless.a tests/visual/p006_visual_cube_tower.c $(OBJ_GLAD) | build
	$(CC) $(CFLAGS) $(RENDERER_TEST_CFLAGS) tests/visual/p006_visual_cube_tower.c \
build/libheadless.a $(OBJ_GLAD) -o $@ $(LDFLAGS) $(RENDERER_TEST_LIBS)
build/p005_visual_ragdoll_drop: build/libheadless.a tests/visual/p005_visual_ragdoll_drop.c $(OBJ_GLAD) | build
	$(CC) $(CFLAGS) $(RENDERER_TEST_CFLAGS) tests/visual/p005_visual_ragdoll_drop.c \
$(SRC_ALL) $(OBJ_GLAD) -o $@ $(LDFLAGS) $(RENDERER_TEST_LIBS)
build/p005_visual_anim_force: build/libheadless.a tests/visual/p005_visual_anim_force.c $(OBJ_GLAD) | build
	$(CC) $(CFLAGS) $(RENDERER_TEST_CFLAGS) tests/visual/p005_visual_anim_force.c \
$(SRC_ALL) $(OBJ_GLAD) -o $@ $(LDFLAGS) $(RENDERER_TEST_LIBS)
build/p005_visual_ik_ground: build/libheadless.a tests/visual/p005_visual_ik_ground.c $(OBJ_GLAD) | build
	$(CC) $(CFLAGS) $(RENDERER_TEST_CFLAGS) tests/visual/p005_visual_ik_ground.c \
$(SRC_ALL) $(OBJ_GLAD) -o $@ $(LDFLAGS) $(RENDERER_TEST_LIBS)
build/p005_visual_constraint_converge: build/libheadless.a tests/visual/p005_visual_constraint_converge.c $(OBJ_GLAD) | build
	$(CC) $(CFLAGS) $(RENDERER_TEST_CFLAGS) tests/visual/p005_visual_constraint_converge.c \
$(SRC_ALL) $(OBJ_GLAD) -o $@ $(LDFLAGS) $(RENDERER_TEST_LIBS)
build/p004_scene_graph_tests: build/liball.a tests/p004_renderer_scene_graph_tests.c | build
	$(CC) $(CFLAGS) tests/p004_renderer_scene_graph_tests.c build/liball.a -o $@ $(LDFLAGS)
build/p005_constraint_types_tests: build/libheadless.a tests/p005_constraint_types_tests.c | build
	$(CC) $(CFLAGS) tests/p005_constraint_types_tests.c build/libheadless.a -o $@ $(LDFLAGS)
build/p005_constraint_solver_tests: build/libheadless.a tests/p005_constraint_solver_tests.c | build
	$(CC) $(CFLAGS) tests/p005_constraint_solver_tests.c build/libheadless.a -o $@ $(LDFLAGS)
build/p005_ik_solver_tests: build/libheadless.a tests/p005_ik_solver_tests.c | build
	$(CC) $(CFLAGS) tests/p005_ik_solver_tests.c build/libheadless.a -o $@ $(LDFLAGS)
build/p005_copy_track_tests: build/libheadless.a tests/p005_copy_track_tests.c | build
	$(CC) $(CFLAGS) tests/p005_copy_track_tests.c build/libheadless.a -o $@ $(LDFLAGS)
build/p005_limit_tests: build/libheadless.a tests/p005_limit_tests.c | build
	$(CC) $(CFLAGS) tests/p005_limit_tests.c build/libheadless.a -o $@ $(LDFLAGS)
build/p005_transform_map_tests: build/libheadless.a tests/p005_transform_map_tests.c | build
	$(CC) $(CFLAGS) tests/p005_transform_map_tests.c build/libheadless.a -o $@ $(LDFLAGS)
build/p005_surface_vol_tests: build/libheadless.a tests/p005_surface_vol_tests.c | build
	$(CC) $(CFLAGS) tests/p005_surface_vol_tests.c build/libheadless.a -o $@ $(LDFLAGS)
build/p005_joint_motor_tests: build/libheadless.a tests/p005_joint_motor_tests.c | build
	$(CC) $(CFLAGS) tests/p005_joint_motor_tests.c build/libheadless.a -o $@ $(LDFLAGS)
build/p005_fskel_tests: build/libheadless.a tests/p005_fskel_tests.c | build
	$(CC) $(CFLAGS) tests/p005_fskel_tests.c build/libheadless.a -o $@ $(LDFLAGS)
build/p005_joint_types_tests: build/libheadless.a tests/p005_joint_types_tests.c | build
	$(CC) $(CFLAGS) tests/p005_joint_types_tests.c build/libheadless.a -o $@ $(LDFLAGS)
build/p110_joint_twist_tests: build/libheadless.a tests/p110_joint_twist_tests.c | build
	$(CC) $(CFLAGS) tests/p110_joint_twist_tests.c build/libheadless.a -o $@ $(LDFLAGS)
build/p111_joint_driver_tests: build/libheadless.a tests/p111_joint_driver_tests.c | build
	$(CC) $(CFLAGS) tests/p111_joint_driver_tests.c build/libheadless.a -o $@ $(LDFLAGS)
build/p112_joint_driver_extended_tests: build/libheadless.a tests/p112_joint_driver_extended_tests.c | build
	$(CC) $(CFLAGS) tests/p112_joint_driver_extended_tests.c build/libheadless.a -o $@ $(LDFLAGS)
build/p200_panel_layout_tests: build/libheadless.a tests/p200_panel_layout_tests.c | build
	$(CC) $(CFLAGS) tests/p200_panel_layout_tests.c build/libheadless.a -o $@ $(LDFLAGS)
build/p200b_clay_backend_tests: build/libheadless.a tests/p200b_clay_backend_tests.c | build
	$(CC) $(CFLAGS) -Iextern/clay tests/p200b_clay_backend_tests.c build/libheadless.a -o $@ $(LDFLAGS)
build/p200c_tui_panel_tests: build/libheadless.a tests/p200c_tui_panel_tests.c | build
	$(CC) $(CFLAGS) tests/p200c_tui_panel_tests.c build/libheadless.a -o $@ $(LDFLAGS)
build/p200d_scene_connection_tests: build/libheadless.a tests/p200d_scene_connection_tests.c | build
	$(CC) $(CFLAGS) tests/p200d_scene_connection_tests.c build/libheadless.a -o $@ $(LDFLAGS)
build/p200e_scene_sync_tests: build/libheadless.a tests/p200e_scene_sync_tests.c | build
	$(CC) $(CFLAGS) tests/p200e_scene_sync_tests.c build/libheadless.a -o $@ $(LDFLAGS)
build/p200f_autosave_tests: build/libheadless.a tests/p200f_autosave_tests.c | build
	$(CC) $(CFLAGS) tests/p200f_autosave_tests.c build/libheadless.a -o $@ $(LDFLAGS)
build/p201_pivot_offset_tests: build/libheadless.a tests/p201_pivot_offset_tests.c | build
	$(CC) $(CFLAGS) tests/p201_pivot_offset_tests.c build/libheadless.a -o $@ $(LDFLAGS)
build/p202_viewport_camera_tests: build/libheadless.a tests/p202_viewport_camera_tests.c | build
	$(CC) $(CFLAGS) tests/p202_viewport_camera_tests.c build/libheadless.a -o $@ $(LDFLAGS)
build/scene_viewport_render_tests: build/liball.a tests/scene_viewport_render_tests.c | build
	$(CC) $(CFLAGS) tests/scene_viewport_render_tests.c build/liball.a -o $@ $(LDFLAGS)
build/scene_viewport_mesh_tests: build/liball.a tests/scene_viewport_mesh_tests.c | build
	$(CC) $(CFLAGS) tests/scene_viewport_mesh_tests.c build/liball.a -o $@ $(LDFLAGS)
build/ctrl_cmd_parse_tests: build/libheadless.a tests/editor/ctrl_cmd_parse_tests.c | build
	$(CC) $(CFLAGS) tests/editor/ctrl_cmd_parse_tests.c build/libheadless.a -o $@ $(LDFLAGS)
build/p203_gizmo_hit_tests: build/libheadless.a tests/p203_gizmo_hit_tests.c | build
	$(CC) $(CFLAGS) tests/p203_gizmo_hit_tests.c build/libheadless.a -o $@ $(LDFLAGS)
build/p204_selection_raycast_tests: build/libheadless.a tests/p204_selection_raycast_tests.c | build
	$(CC) $(CFLAGS) tests/p204_selection_raycast_tests.c build/libheadless.a -o $@ $(LDFLAGS)
build/p205_outliner_tree_tests: build/libheadless.a tests/p205_outliner_tree_tests.c | build
	$(CC) $(CFLAGS) tests/p205_outliner_tree_tests.c build/libheadless.a -o $@ $(LDFLAGS)
build/p205b_inspector_widget_tests: build/libheadless.a tests/p205b_inspector_widget_tests.c | build
	$(CC) $(CFLAGS) tests/p205b_inspector_widget_tests.c build/libheadless.a -o $@ $(LDFLAGS)
build/p205c_mode_manager_tests: build/libheadless.a tests/p205c_mode_manager_tests.c | build
	$(CC) $(CFLAGS) tests/p205c_mode_manager_tests.c build/libheadless.a -o $@ $(LDFLAGS)
build/p205d_toolbar_tests: build/libheadless.a tests/p205d_toolbar_tests.c | build
	$(CC) $(CFLAGS) tests/p205d_toolbar_tests.c build/libheadless.a -o $@ $(LDFLAGS)
build/p206a_snap_state_tests: build/libheadless.a tests/p206a_snap_state_tests.c | build
	$(CC) $(CFLAGS) tests/p206a_snap_state_tests.c build/libheadless.a -o $@ $(LDFLAGS)
build/p206b_pivot_tests: build/libheadless.a tests/p206b_pivot_tests.c | build
	$(CC) $(CFLAGS) tests/p206b_pivot_tests.c build/libheadless.a -o $@ $(LDFLAGS)
build/p206b_snap_gizmo_tests: build/libheadless.a tests/p206b_snap_gizmo_tests.c | build
	$(CC) $(CFLAGS) tests/p206b_snap_gizmo_tests.c build/libheadless.a -o $@ $(LDFLAGS)
build/pivot_edit_tests: build/libheadless.a tests/editor/pivot_edit_tests.c | build
	$(CC) $(CFLAGS) tests/editor/pivot_edit_tests.c build/libheadless.a -o $@ $(LDFLAGS)
build/per_object_gizmo_tests: build/libheadless.a tests/editor/per_object_gizmo_tests.c | build
	$(CC) $(CFLAGS) tests/editor/per_object_gizmo_tests.c build/libheadless.a -o $@ $(LDFLAGS)
build/snap_mesh_cache_tests: build/libheadless.a tests/editor/snap_mesh_cache_tests.c | build
	$(CC) $(CFLAGS) tests/editor/snap_mesh_cache_tests.c build/libheadless.a -o $@ $(LDFLAGS)
build/snap_ray_triangle_tests: build/libheadless.a tests/editor/snap_ray_triangle_tests.c | build
	$(CC) $(CFLAGS) tests/editor/snap_ray_triangle_tests.c build/libheadless.a -o $@ $(LDFLAGS)
build/snap_surface_cast_tests: build/libheadless.a tests/editor/snap_surface_cast_tests.c | build
	$(CC) $(CFLAGS) tests/editor/snap_surface_cast_tests.c build/libheadless.a -o $@ $(LDFLAGS)
build/snap_surface_apply_tests: build/libheadless.a tests/editor/snap_surface_apply_tests.c | build
	$(CC) $(CFLAGS) tests/editor/snap_surface_apply_tests.c build/libheadless.a -o $@ $(LDFLAGS)
build/tri_tri_tests: build/libheadless.a tests/physics/tri_tri_tests.c | build
	$(CC) $(CFLAGS) tests/physics/tri_tri_tests.c build/libheadless.a -o $@ $(LDFLAGS)
build/mesh_mesh_tests: build/libheadless.a tests/physics/mesh_mesh_tests.c | build
	$(CC) $(CFLAGS) tests/physics/mesh_mesh_tests.c build/libheadless.a -o $@ $(LDFLAGS)
build/snap_depenetrate_tests: build/libheadless.a tests/editor/snap_depenetrate_tests.c | build
	$(CC) $(CFLAGS) tests/editor/snap_depenetrate_tests.c build/libheadless.a -o $@ $(LDFLAGS)
build/snap_mesh_retain_convex_tests: build/libheadless.a tests/editor/snap_mesh_retain_convex_tests.c | build
	$(CC) $(CFLAGS) tests/editor/snap_mesh_retain_convex_tests.c build/libheadless.a -o $@ $(LDFLAGS)
build/collision_mesh_tests: build/libheadless.a tests/editor/collision_mesh_tests.c | build
	$(CC) $(CFLAGS) tests/editor/collision_mesh_tests.c build/libheadless.a -o $@ $(LDFLAGS)
build/collision_mesh_asset_tests: build/libheadless.a tests/asset/collision_mesh_asset_tests.c | build
	$(CC) $(CFLAGS) tests/asset/collision_mesh_asset_tests.c build/libheadless.a -o $@ $(LDFLAGS)
build/asset_browser_tests: build/libheadless.a tests/editor/asset_browser_tests.c | build
	$(CC) $(CFLAGS) tests/editor/asset_browser_tests.c build/libheadless.a -o $@ $(LDFLAGS)
build/asset_load_tests: build/libheadless.a tests/editor/asset_load_tests.c | build
	$(CC) $(CFLAGS) tests/editor/asset_load_tests.c build/libheadless.a -o $@ $(LDFLAGS)
build/skeleton_registry_tests: build/libheadless.a tests/editor/skeleton_registry_tests.c | build
	$(CC) $(CFLAGS) tests/editor/skeleton_registry_tests.c build/libheadless.a -o $@ $(LDFLAGS)
build/vm_cache_tests: build/libheadless.a tests/editor/vm_cache_tests.c | build
	$(CC) $(CFLAGS) tests/editor/vm_cache_tests.c build/libheadless.a -o $@ $(LDFLAGS)
build/entity_json_tests: build/libheadless.a tests/editor/entity_json_tests.c | build
	$(CC) $(CFLAGS) tests/editor/entity_json_tests.c build/libheadless.a -o $@ $(LDFLAGS)
build/snap_mesh_decompose_tests: build/libheadless.a tests/editor/snap_mesh_decompose_tests.c | build
	$(CC) $(CFLAGS) tests/editor/snap_mesh_decompose_tests.c build/libheadless.a -o $@ $(LDFLAGS)
build/snap_decompose_cache_tests: build/libheadless.a tests/editor/snap_decompose_cache_tests.c | build
	$(CC) $(CFLAGS) tests/editor/snap_decompose_cache_tests.c build/libheadless.a -o $@ $(LDFLAGS)
build/collision_mesh_path_tests: build/libheadless.a tests/editor/collision_mesh_path_tests.c | build
	$(CC) $(CFLAGS) tests/editor/collision_mesh_path_tests.c build/libheadless.a -o $@ $(LDFLAGS)
build/mesh_bone_segment_tests: build/libheadless.a tests/editor/mesh_bone_segment_tests.c | build
	$(CC) $(CFLAGS) tests/editor/mesh_bone_segment_tests.c build/libheadless.a -o $@ $(LDFLAGS)
build/mesh_bone_collision_tests: build/libheadless.a tests/editor/mesh_bone_collision_tests.c | build
	$(CC) $(CFLAGS) tests/editor/mesh_bone_collision_tests.c build/libheadless.a -o $@ $(LDFLAGS)
build/viewport_mesh_type_tests: build/libheadless.a tests/editor/viewport_mesh_type_tests.c | build
	$(CC) $(CFLAGS) tests/editor/viewport_mesh_type_tests.c build/libheadless.a -o $@ $(LDFLAGS)
build/viewport_skel_promote_tests: build/libheadless.a tests/editor/viewport_skel_promote_tests.c | build
	$(CC) $(CFLAGS) tests/editor/viewport_skel_promote_tests.c build/libheadless.a -o $@ $(LDFLAGS)
build/collider_entity_type_tests: build/libheadless.a tests/editor/collider_entity_type_tests.c | build
	$(CC) $(CFLAGS) tests/editor/collider_entity_type_tests.c build/libheadless.a -o $@ $(LDFLAGS)
build/bone_selection_tests: build/libheadless.a tests/editor/bone_selection_tests.c | build
	$(CC) $(CFLAGS) tests/editor/bone_selection_tests.c build/libheadless.a -o $@ $(LDFLAGS)
build/bone_overlay_tests: build/libheadless.a tests/editor/bone_overlay_tests.c | build
	$(CC) $(CFLAGS) tests/editor/bone_overlay_tests.c build/libheadless.a -o $@ $(LDFLAGS)
build/bone_pick_tests: build/libheadless.a tests/editor/bone_pick_tests.c | build
	$(CC) $(CFLAGS) tests/editor/bone_pick_tests.c build/libheadless.a -o $@ $(LDFLAGS)
build/prefab_mode_tests: build/libheadless.a tests/editor/prefab_mode_tests.c | build
	$(CC) $(CFLAGS) tests/editor/prefab_mode_tests.c build/libheadless.a -o $@ $(LDFLAGS)
build/prefab_bone_parent_tests: build/libheadless.a tests/editor/prefab_bone_parent_tests.c | build
	$(CC) $(CFLAGS) tests/editor/prefab_bone_parent_tests.c build/libheadless.a -o $@ $(LDFLAGS)
build/prefab_outliner_tests: build/libheadless.a tests/editor/prefab_outliner_tests.c | build
	$(CC) $(CFLAGS) tests/editor/prefab_outliner_tests.c build/libheadless.a -o $@ $(LDFLAGS)
build/prefab_hull_build_tests: build/libheadless.a tests/editor/prefab_hull_build_tests.c | build
	$(CC) $(CFLAGS) tests/editor/prefab_hull_build_tests.c build/libheadless.a -o $@ $(LDFLAGS)
build/prefab_hull_cache_tests: build/libheadless.a tests/editor/prefab_hull_cache_tests.c | build
	$(CC) $(CFLAGS) tests/editor/prefab_hull_cache_tests.c build/libheadless.a -o $@ $(LDFLAGS)
build/prefab_def_tests: build/libheadless.a tests/editor/prefab_def_tests.c | build
	$(CC) $(CFLAGS) tests/editor/prefab_def_tests.c build/libheadless.a -o $@ $(LDFLAGS)
build/prefab_serialize_tests: build/libheadless.a tests/editor/prefab_serialize_tests.c | build
	$(CC) $(CFLAGS) tests/editor/prefab_serialize_tests.c build/libheadless.a -o $@ $(LDFLAGS)
build/prefab_collect_tests: build/libheadless.a tests/editor/prefab_collect_tests.c | build
	$(CC) $(CFLAGS) tests/editor/prefab_collect_tests.c build/libheadless.a -o $@ $(LDFLAGS)
build/prefab_pose_apply_tests: build/libheadless.a tests/editor/prefab_pose_apply_tests.c | build
	$(CC) $(CFLAGS) tests/editor/prefab_pose_apply_tests.c build/libheadless.a -o $@ $(LDFLAGS)
build/bone_pose_store_tests: build/libheadless.a tests/editor/bone_pose_store_tests.c | build
	$(CC) $(CFLAGS) tests/editor/bone_pose_store_tests.c build/libheadless.a -o $@ $(LDFLAGS)
build/asset_ref_widget_tests: build/libheadless.a tests/editor/asset_ref_widget_tests.c | build
	$(CC) $(CFLAGS) tests/editor/asset_ref_widget_tests.c build/libheadless.a -o $@ $(LDFLAGS)
build/prefab_child_spawn_tests: build/libheadless.a tests/editor/prefab_child_spawn_tests.c | build
	$(CC) $(CFLAGS) tests/editor/prefab_child_spawn_tests.c build/libheadless.a -o $@ $(LDFLAGS)
build/skel_promote_tests: build/libheadless.a tests/editor/skel_promote_tests.c | build
	$(CC) $(CFLAGS) tests/editor/skel_promote_tests.c build/libheadless.a -o $@ $(LDFLAGS)
build/bone_gizmo_tests: build/libheadless.a tests/editor/bone_gizmo_tests.c | build
	$(CC) $(CFLAGS) tests/editor/bone_gizmo_tests.c build/libheadless.a -o $@ $(LDFLAGS)
build/scene_editor: build/liball.a tools/scene_editor_main.c | build
	$(CC) $(CFLAGS) $(RENDERER_TEST_CFLAGS) tools/scene_editor_main.c build/liball.a -o $@ $(LDFLAGS) $(RENDERER_TEST_LIBS)
build:


test: $(BIN_HEADLESS) build/p008_net_replication_protocol_tests build/p000_job_queue_sharding_tests build/p000_job_queue_diagnostics_tests build/p000_ws_deque_tests build/p007_net_client_rx_tests build/p007_net_client_rx_udp_topic_tests build/p007_net_topic_dispatch_tests build/npc_kg_astar_tests build/npc_kg_spatial_tests
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
	&& ./build/p120_static_bvh_raycast_tests \
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
	&& ./build/p121_ccd_dynamic_tests \
	&& ./build/p122_joint_properties_tests \
	&& ./build/p123_muscle_activation_tests \
	&& ./build/p124_muscle_force_curve_tests \
	&& ./build/p125_muscle_tendon_tests \
	&& ./build/p126_muscle_geometry_tests \
	&& ./build/p127_muscle_unit_tests \
	&& ./build/p128_muscle_pair_tests \
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
	&& ./build/edit_script_rebase_tests \
	&& ./build/entity_hide_tests \
	&& ./build/viewport_bsp_tests \
	&& ./build/nav_mode_tests \
	&& ./build/shading_mode_tests \
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
	&& ./build/aegis_runtime_tests \
	&& ./build/aegis_ops_entity_tests \
	&& ./build/aegis_ops_update_tests \
	&& ./build/aegis_async_buffer_tests \
	&& ./build/aegis_ops_async_tests \
	&& ./build/aegis_async_execute_tests \
	&& ./build/aegis_llm_prompt_tests \
	&& ./build/aegis_sense_tests \
	&& ./build/aegis_tools_tests \
	&& ./build/aegis_ops_signal_tests \
	&& ./build/aegis_ops_await_tests \
	&& ./build/aegis_runtime_idle_tests \
	&& ./build/aegis_signal_integration_tests \
	&& ./build/phys_pair_set_tests \
	&& ./build/phys_contact_begin_tests \
	&& ./build/phys_overlap_begin_tests \
	&& ./build/collision_event_integration_tests \
	&& ./build/turret_script_e2e_tests \
	&& ./build/ctrl_cmd_parse_tests \
	&& ./build/pivot_edit_tests \
	&& ./build/npc_knowledge_graph_tests \
	&& ./build/npc_kg_astar_tests \
	&& ./build/npc_faiss_tests \
	&& ./build/npc_svo_tests \
	&& ./build/npc_sense_tests \
	&& ./build/npc_scent_tests \
	&& ./build/npc_nav_graph_tests \
	&& ./build/npc_pathfind_tests \
	&& ./build/npc_nav_action_tests \
	&& ./build/npc_nav_integration_tests \
	&& ./build/npc_trade_state_tests \
	&& ./build/npc_state_tests \
	&& ./build/npc_demo_integration_tests \
	&& ./build/npc_kg_spatial_tests \
	&& ./build/lm_visibility_tests \
	&& ./build/lm_sh_tests \
	&& ./build/lm_kdtree_tests \
	&& ./build/lm_lightmap_tests \
	&& ./build/lm_light_tests \
	&& ./build/lm_material_tests \
	&& ./build/lm_atlas_tests \
	&& ./build/lm_direct_tests \
	&& ./build/lm_indirect_tests \
	&& ./build/lm_solve_tests \
	&& ./build/lm_svo_material_tests \
	&& ./build/lm_farfield_tests \
	&& ./build/lm_sky_tests \
	&& ./build/lm_svo_mip_tests \
	&& ./build/lm_svo_voxelize_tests \
	&& ./build/obj_mesh_load_tests \
	&& ./build/dmesh_load_tests \
	&& ./build/lm_mesh_luxel_tests \
	&& ./build/lm_mesh_bake_tests \
	&& ./build/lm_lightmap_file_tests

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
	./build/p004_tests && ./build/p004_shader_tests && ./build/p004_texture_tests && ./build/p004_material_tests && ./build/p004_pbr_shader_tests && ./build/p004_light_store_tests && ./build/p004_scene_tests && ./build/p004_depth_prepass_tests && ./build/p004_cluster_tests && ./build/p004_buffer_tests && ./build/p004_gpu_cmd_queue_tests && ./build/p004_gpu_registry_tests && ./build/p004_shadow_slotmap_tests \
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

SRD_OBJS = $(SRD2_C_OBJS)

build/demo_client: build/liball.a tests/examples/demo_client.c tests/examples/cornell_demo.c $(SRD_OBJS) $(SYMX_LIB) $(SYMX_FMT) | build
	$(CC) $(CFLAGS) $(RENDERER_TEST_CFLAGS) tests/examples/demo_client.c tests/examples/cornell_demo.c $(SRD_OBJS) build/liball.a $(SYMX_LIB) $(SYMX_FMT) build/liball.a -o $@ $(LDFLAGS) $(RENDERER_TEST_LIBS) -ldl

demo_client:
	@$(MAKE) build/demo_client
	@echo "Built: build/demo_client"
	@echo "Usage: ./build/demo_client <server_ip> <port> [duration_s] [--headless]"

build/editor_server: build/libheadless.a tools/editor_server.c | build
	$(CC) $(CFLAGS) tools/editor_server.c build/libheadless.a -o $@ $(LDFLAGS)
build/editor_tui: build/libheadless.a tools/editor_tui.c | build
	$(CC) $(CFLAGS) tools/editor_tui.c build/libheadless.a -o $@ $(LDFLAGS)

editor_tui:
	@$(MAKE) build/editor_tui
	@echo "Built: build/editor_tui"
	@echo "Usage: ./build/editor_tui [host:port] [--exec <script>]"

# ── Procgen targets ───────────────────────────────────────────
PROCGEN_TESTS :=

# Procgen test binaries will be added here as they are created:
# PROCGEN_TESTS += build/procgen_types_tests
# PROCGEN_TESTS += build/procgen_tokenize_tests
# PROCGEN_TESTS += build/procgen_rasterize_tests
# PROCGEN_TESTS += build/procgen_grammar_blockout_tests
# PROCGEN_TESTS += build/procgen_serialize_tests
# PROCGEN_TESTS += build/procgen_architect_tests
# PROCGEN_TESTS += build/procgen_critic_tests

# SRD tests (depend on SymX)
build/srd_corridor_energy_tests: tests/procgen/srd/srd_corridor_energy_tests.cpp src/procgen/srd/srd_energy.cpp $(SYMX_LIB) $(SYMX_FMT) | build
	$(CXX) $(SYMX_FLAGS) -Iinclude tests/procgen/srd/srd_corridor_energy_tests.cpp src/procgen/srd/srd_energy.cpp $(SYMX_LIB) $(SYMX_FMT) -ldl -fopenmp -o $@

build/srd_stair_overlap_tests: tests/procgen/srd/srd_stair_overlap_tests.cpp src/procgen/srd/srd_energy.cpp $(SYMX_LIB) $(SYMX_FMT) | build
	$(CXX) $(SYMX_FLAGS) -Iinclude tests/procgen/srd/srd_stair_overlap_tests.cpp src/procgen/srd/srd_energy.cpp $(SYMX_LIB) $(SYMX_FMT) -ldl -fopenmp -o $@

build/srd_grammar_tests: tests/procgen/srd/srd_grammar_tests.c src/procgen/procgen_srd_grammar.c src/procgen/procgen_srd_types.c | build
	$(CC) $(CFLAGS) tests/procgen/srd/srd_grammar_tests.c src/procgen/procgen_srd_grammar.c src/procgen/procgen_srd_types.c -o $@ -lm

PROCGEN_TESTS += build/srd_stair_overlap_tests
build/srd_energy_tests: tests/procgen/srd/srd_energy_tests.cpp $(SYMX_LIB) $(SYMX_FMT) | build
	$(CXX) $(SYMX_FLAGS) -Iinclude tests/procgen/srd/srd_energy_tests.cpp $(SYMX_LIB) $(SYMX_FMT) -ldl -fopenmp -o $@

build/srd_m1_smoke: tests/procgen/srd/srd_m1_smoke.cpp $(OBJDIR)/src/procgen/procgen_ascii_parse.o $(OBJDIR)/src/procgen/procgen_srd_types.o | build
	$(CXX) $(CFLAGS) -std=c++17 tests/procgen/srd/srd_m1_smoke.cpp $(OBJDIR)/src/procgen/procgen_ascii_parse.o $(OBJDIR)/src/procgen/procgen_srd_types.o -o $@ -lm

build/srd_anneal_tests: tests/procgen/srd/srd_anneal_tests.cpp src/procgen/srd/srd_anneal.c | build
	$(CXX) $(CFLAGS) -Iinclude -xc++ -std=c++17 tests/procgen/srd/srd_anneal_tests.cpp src/procgen/srd/srd_anneal.c -o $@ -lm

build/srd_rewrite_tests: tests/procgen/srd/srd_rewrite_tests.cpp src/procgen/procgen_srd_rewrite.c src/procgen/procgen_srd_grammar.c src/procgen/procgen_srd_types.c | build
	$(CXX) $(CFLAGS) -xc++ -std=c++17 tests/procgen/srd/srd_rewrite_tests.cpp src/procgen/procgen_srd_rewrite.c src/procgen/procgen_srd_grammar.c src/procgen/procgen_srd_types.c -o $@ -lm

build/srd_sampler_tests: tests/procgen/srd/srd_sampler_tests.c src/procgen/srd/srd_sampler.c src/procgen/procgen_srd_types.c | build
	$(CC) $(CFLAGS) tests/procgen/srd/srd_sampler_tests.c src/procgen/srd/srd_sampler.c src/procgen/procgen_srd_types.c -o $@ -lm

build/srd_m2_smoke: tests/procgen/srd/srd_m2_energy_smoke.cpp src/procgen/srd/srd_energy.cpp $(SYMX_LIB) $(SYMX_FMT) | build
	$(CXX) $(SYMX_FLAGS) -Iinclude tests/procgen/srd/srd_m2_energy_smoke.cpp src/procgen/srd/srd_energy.cpp $(SYMX_LIB) $(SYMX_FMT) -ldl -fopenmp -o $@

PROCGEN_TESTS += build/srd_m2_smoke
build/srd_pde_tests: tests/procgen/srd/srd_pde_tests.cpp src/procgen/srd/srd_eikonal.cpp src/procgen/srd/srd_transport.cpp | build
	$(CXX) $(CFLAGS) -xc++ -std=c++17 tests/procgen/srd/srd_pde_tests.cpp src/procgen/srd/srd_eikonal.cpp src/procgen/srd/srd_transport.cpp -o $@ -lm

build/srd_loss_primitives_tests: tests/procgen/srd/srd_loss_primitives_tests.cpp src/procgen/srd/srd_loss_primitives.cpp src/procgen/srd/srd_eikonal.cpp src/procgen/srd/srd_transport.cpp src/procgen/procgen_srd_types.c | build
	$(CXX) $(CFLAGS) -xc++ -std=c++17 -Iinclude tests/procgen/srd/srd_loss_primitives_tests.cpp src/procgen/srd/srd_loss_primitives.cpp src/procgen/srd/srd_eikonal.cpp src/procgen/srd/srd_transport.cpp src/procgen/procgen_srd_types.c -o $@ -lm

build/srd_loss_compiler_tests: tests/procgen/srd/srd_loss_compiler_tests.cpp src/procgen/srd/srd_loss_compiler.cpp src/procgen/procgen_srd_types.c | build
	$(CXX) $(CFLAGS) -xc++ -std=c++17 -Iinclude tests/procgen/srd/srd_loss_compiler_tests.cpp src/procgen/srd/srd_loss_compiler.cpp src/procgen/procgen_srd_types.c -o $@ -lm

# Old C++ bridge test removed — replaced by pure-C srd_bridge_tests in SRD2 section

build/srd_m3_m4_smoke: tests/procgen/srd/srd_m3_m4_smoke.cpp src/procgen/srd/srd_bridge.cpp src/procgen/srd/srd_optimizer.cpp src/procgen/srd/srd_energy.cpp src/procgen/srd/srd_loss_compiler.cpp src/procgen/srd/srd_loss_primitives.cpp src/procgen/srd/srd_loss_gradient.cpp src/procgen/srd/srd_eikonal.cpp src/procgen/srd/srd_transport.cpp src/procgen/procgen_ascii_parse.c src/procgen/procgen_srd_grammar.c src/procgen/procgen_srd_rewrite.c src/procgen/procgen_srd_types.c src/procgen/srd/srd_anneal.c $(SYMX_LIB) $(SYMX_FMT) | build
	$(CXX) $(SYMX_FLAGS) -Iinclude tests/procgen/srd/srd_m3_m4_smoke.cpp src/procgen/srd/srd_bridge.cpp src/procgen/srd/srd_optimizer.cpp src/procgen/srd/srd_energy.cpp src/procgen/srd/srd_loss_compiler.cpp src/procgen/srd/srd_loss_primitives.cpp src/procgen/srd/srd_loss_gradient.cpp src/procgen/srd/srd_eikonal.cpp src/procgen/srd/srd_transport.cpp src/procgen/procgen_ascii_parse.c src/procgen/procgen_srd_grammar.c src/procgen/procgen_srd_rewrite.c src/procgen/procgen_srd_types.c src/procgen/srd/srd_anneal.c $(SYMX_LIB) $(SYMX_FMT) -ldl -fopenmp -o $@

SRD_TEST_C_SRCS  := tests/procgen/srd/srd_svo_integration_tests.c src/procgen/procgen_svo_builder.c src/npc/nav/npc_svo_init.c src/procgen/procgen_srd_types.c src/procgen/procgen_ascii_parse.c src/procgen/procgen_srd_grammar.c src/procgen/procgen_srd_rewrite.c src/procgen/srd/srd_anneal.c
SRD_TEST_CXX_SRCS := src/procgen/srd/srd_bridge.cpp src/procgen/srd/srd_optimizer.cpp src/procgen/srd/srd_energy.cpp src/procgen/srd/srd_loss_compiler.cpp src/procgen/srd/srd_loss_primitives.cpp src/procgen/srd/srd_loss_gradient.cpp src/procgen/srd/srd_eikonal.cpp src/procgen/srd/srd_transport.cpp
SRD_TEST_C_OBJS   := $(patsubst %.c, $(OBJDIR)/%.o, $(SRD_TEST_C_SRCS))
SRD_TEST_CXX_OBJS := $(patsubst %.cpp, $(OBJDIR)/%.o, $(SRD_TEST_CXX_SRCS))

# Build rule for SRD C++ objects (need SymX includes)
$(OBJDIR)/src/procgen/srd/%.o: src/procgen/srd/%.cpp | build
	$(CXX) $(SYMX_FLAGS) -Iinclude -c $< -o $@

$(OBJDIR)/tests/procgen/srd/%.o: tests/procgen/srd/%.cpp | build
	$(CXX) $(SYMX_FLAGS) -Iinclude -c $< -o $@

build/srd_svo_integration_tests: $(SRD_TEST_C_OBJS) $(SRD_TEST_CXX_OBJS) $(SYMX_LIB) $(SYMX_FMT) | build
	$(CXX) $(SRD_TEST_C_OBJS) $(SRD_TEST_CXX_OBJS) $(SYMX_LIB) $(SYMX_FMT) -ldl -fopenmp -lm -o $@

PROCGEN_TESTS += build/srd_svo_integration_tests

PROCGEN_TESTS += build/srd_svo_integration_tests

build/srd_optimizer_tests: tests/procgen/srd/srd_optimizer_tests.cpp src/procgen/srd/srd_optimizer.cpp src/procgen/srd/srd_energy.cpp src/procgen/srd/srd_loss_compiler.cpp src/procgen/srd/srd_loss_primitives.cpp src/procgen/srd/srd_eikonal.cpp src/procgen/srd/srd_transport.cpp src/procgen/procgen_srd_grammar.c src/procgen/procgen_srd_rewrite.c src/procgen/procgen_srd_types.c $(SYMX_LIB) $(SYMX_FMT) | build
	$(CXX) $(SYMX_FLAGS) -Iinclude tests/procgen/srd/srd_optimizer_tests.cpp src/procgen/srd/srd_optimizer.cpp src/procgen/srd/srd_energy.cpp src/procgen/srd/srd_loss_compiler.cpp src/procgen/srd/srd_loss_primitives.cpp src/procgen/srd/srd_eikonal.cpp src/procgen/srd/srd_transport.cpp src/procgen/procgen_srd_grammar.c src/procgen/procgen_srd_rewrite.c src/procgen/procgen_srd_types.c $(SYMX_LIB) $(SYMX_FMT) -ldl -fopenmp -o $@

build/srd_pde_gradient_tests: tests/procgen/srd/srd_pde_gradient_tests.cpp src/procgen/srd/srd_loss_primitives.cpp src/procgen/srd/srd_loss_gradient.cpp src/procgen/srd/srd_eikonal.cpp src/procgen/srd/srd_transport.cpp src/procgen/procgen_srd_types.c | build
	$(CXX) $(CFLAGS) -xc++ -std=c++17 -Iinclude tests/procgen/srd/srd_pde_gradient_tests.cpp src/procgen/srd/srd_loss_primitives.cpp src/procgen/srd/srd_loss_gradient.cpp src/procgen/srd/srd_eikonal.cpp src/procgen/srd/srd_transport.cpp src/procgen/procgen_srd_types.c -o $@ -lm

PROCGEN_TESTS += build/srd_optimizer_tests
# SRD static library (C++ objects + SymX for linking into C tests)
SRD_LIB_CXX_OBJS := $(OBJDIR)/src/procgen/srd/srd_bridge.o \
                    $(OBJDIR)/src/procgen/srd/srd_optimizer.o \
                    $(OBJDIR)/src/procgen/srd/srd_energy.o \
                    $(OBJDIR)/src/procgen/srd/srd_loss_compiler.o \
                    $(OBJDIR)/src/procgen/srd/srd_loss_primitives.o \
                    $(OBJDIR)/src/procgen/srd/srd_loss_gradient.o \
                    $(OBJDIR)/src/procgen/srd/srd_eikonal.o \
                    $(OBJDIR)/src/procgen/srd/srd_transport.o
SRD_LIB_C_OBJS := $(OBJDIR)/src/procgen/procgen_srd_types.o \
                  $(OBJDIR)/src/procgen/procgen_ascii_parse.o \
                  $(OBJDIR)/src/procgen/procgen_srd_grammar.o \
                  $(OBJDIR)/src/procgen/procgen_srd_rewrite.o \
                  $(OBJDIR)/src/procgen/srd/srd_anneal.o \
                  $(OBJDIR)/src/procgen/procgen_svo_builder.o \
                  $(OBJDIR)/src/npc/nav/npc_svo_init.o \
                  $(OBJDIR)/src/procgen/procgen_chunk_mesh.o
build/srd_lib.a: $(SRD_LIB_C_OBJS) $(SRD_LIB_CXX_OBJS) $(SYMX_LIB) $(SYMX_FMT)
	ar rcs $@ $(SRD_LIB_C_OBJS) $(SRD_LIB_CXX_OBJS)

build/srd_m5_smoke: tests/procgen/srd/srd_m5_pipeline_smoke.c $(SRD_LIB_CXX_OBJS) $(SYMX_LIB) $(SYMX_FMT) | build
	$(CC) $(CFLAGS) -c tests/procgen/srd/srd_m5_pipeline_smoke.c -o $(OBJDIR)/tests/procgen/srd/srd_m5_smoke.o
	$(CXX) $(OBJDIR)/tests/procgen/srd/srd_m5_smoke.o $(SRD_LIB_C_OBJS) $(SRD_LIB_CXX_OBJS) $(SYMX_LIB) $(SYMX_FMT) -ldl -fopenmp -lstdc++ -lm -o $@

PROCGEN_TESTS += build/srd_m5_smoke
PROCGEN_TESTS += build/srd_m1_smoke
PROCGEN_TESTS += build/procgen_ascii_parse_tests
PROCGEN_TESTS += build/procgen_architect_tests

build/procgen_architect_tests: tests/procgen/procgen_architect_tests.c build/libheadless.a | build
	$(CC) $(CFLAGS) tests/procgen/procgen_architect_tests.c -o $@ $(LDFLAGS) build/libheadless.a -lcurl -lm

procgen: build/libheadless.a $(SYMX_LIB)
	@echo "procgen objects built via libheadless.a"

procgen-test: $(PROCGEN_TESTS)
	@for t in $(PROCGEN_TESTS); do ./$$t || exit 1; done
	@echo "All procgen tests passed."

procgen-bench:
	@echo "No procgen benchmarks defined yet."

# ── SRD test targets ─────────────────────────────────────────
build/srd_build_test: tests/procgen/srd/srd_build_test.cpp $(SYMX_LIB) $(SYMX_FMT) | build
	$(CXX) $(SYMX_FLAGS) tests/procgen/srd/srd_build_test.cpp $(SYMX_LIB) $(SYMX_FMT) -ldl -fopenmp -o $@

build/srd_types_tests: tests/procgen/srd/srd_types_tests.c src/procgen/procgen_srd_types.c include/ferrum/procgen/procgen_srd_types.h | build
	$(CC) $(CFLAGS) tests/procgen/srd/srd_types_tests.c src/procgen/procgen_srd_types.c -o $@ -lm

build/procgen_ascii_parse_tests: tests/procgen/procgen_ascii_parse_tests.c src/procgen/procgen_ascii_parse.c src/procgen/procgen_srd_types.c include/ferrum/procgen/procgen_ascii_parse.h include/ferrum/procgen/procgen_srd_types.h | build
	$(CC) $(CFLAGS) tests/procgen/procgen_ascii_parse_tests.c src/procgen/procgen_ascii_parse.c src/procgen/procgen_srd_types.c -o $@ -lm

test-procgen: procgen-test

# ── SRD v2 (libtorch-based) ────────────────────────────────────
# pip-installed torch; override with TORCH_DIR= if needed
TORCH_DIR ?= $(shell python3 -c "import torch; print(torch.__path__[0])" 2>/dev/null)
TORCH_INC  = -I$(TORCH_DIR)/include -I$(TORCH_DIR)/include/torch/csrc/api/include
TORCH_LIB  = -L$(TORCH_DIR)/lib -Wl,-rpath,$(TORCH_DIR)/lib -ltorch -ltorch_cpu -lc10
SRD2_CXXFLAGS = -std=c++17 -Wno-unused-parameter -Iinclude $(TORCH_INC) -D_GLIBCXX_USE_CXX11_ABI=1
SRD2_CFLAGS   = -std=c11 -Wall -Wextra -Wno-switch -Wno-unused-parameter -Iinclude

# C sources for the new SRD modules
SRD2_C_SRCS := src/procgen/srd/srd_descent_config.c \
               src/procgen/srd/srd_grammar.c \
               src/procgen/srd/srd_sdf_grid.c \
               src/procgen/srd/srd_sdf_grid_ops.c \
               src/procgen/srd/srd_sdf_grid_stamp.c \
               src/procgen/srd/srd_room_map.c \
               src/procgen/srd/srd_room_map_ops.c \
               src/procgen/srd/srd_room_map_adj.c \
               src/procgen/srd/srd_seed_init.c \
               src/procgen/srd/srd_sdf_to_svo.c \
               src/procgen/srd/srd_grid_critic.c \
               src/procgen/srd/srd_grid_critic_terms.c \
               src/procgen/srd/srd_rules_wall.c \
               src/procgen/srd/srd_rules_wall_shape.c \
               src/procgen/srd/srd_rules_corner.c \
               src/procgen/srd/srd_rules_height.c \
               src/procgen/srd/srd_rules_vcorridor.c \
               src/procgen/srd/srd_rules_vfeature.c \
               src/procgen/srd/srd_rules_embellish.c \
               src/procgen/srd/srd_room_map_copy.c \
               src/procgen/srd/srd_voxel_rule_table.c \
               src/procgen/srd/srd_descent_loop.c \
               src/procgen/srd/srd_bridge.c \
               src/procgen/srd/srd_sdf_raycast.c \
               tests/srd_stubs.c
SRD2_C_OBJS := $(patsubst %.c,$(OBJDIR)/%.o,$(SRD2_C_SRCS))

# C++ sources for the new SRD modules (cleared — loop is now pure C)
SRD2_CXX_SRCS :=
SRD2_CXX_OBJS :=

# Pattern rules for SRD2 objects
$(OBJDIR)/src/procgen/srd/%.o: src/procgen/srd/%.c | build
	@mkdir -p $(dir $@)
	$(CC) $(SRD2_CFLAGS) -c $< -o $@

$(OBJDIR)/src/procgen/srd/%.o: src/procgen/srd/%.cpp | build
	@mkdir -p $(dir $@)
	$(CXX) $(SRD2_CXXFLAGS) -c $< -o $@

$(OBJDIR)/tests/srd_stubs.o: tests/srd_stubs.c | build
	@mkdir -p $(dir $@)
	$(CC) $(SRD2_CFLAGS) -c $< -o $@

SRD2_ALL_OBJS = $(SRD2_C_OBJS)
SRD2_LINK     = -lm
SRD2_SVO_DEPS := src/npc/nav/npc_svo_init.c

# Pure-C tests
build/srd_descent_loop_tests: tests/srd_descent_loop_tests.c $(SRD2_C_OBJS) $(SRD2_SVO_DEPS) | build
	$(CC) $(SRD2_CFLAGS) $< $(SRD2_C_OBJS) $(SRD2_SVO_DEPS) -o $@ -lm

build/srd_bridge_tests: tests/srd_bridge_tests.c $(SRD2_C_OBJS) $(SRD2_SVO_DEPS) | build
	$(CC) $(SRD2_CFLAGS) $< $(SRD2_C_OBJS) $(SRD2_SVO_DEPS) -o $@ -lm

build/srd_property_tests: tests/srd_property_tests.c $(SRD2_C_OBJS) $(SRD2_SVO_DEPS) | build
	$(CC) $(SRD2_CFLAGS) $< $(SRD2_C_OBJS) $(SRD2_SVO_DEPS) -o $@ -lm

build/srd_critic_value_tests: tests/srd_critic_value_tests.c $(SRD2_C_OBJS) $(SRD2_SVO_DEPS) | build
	$(CC) $(SRD2_CFLAGS) $< $(SRD2_C_OBJS) $(SRD2_SVO_DEPS) -o $@ -lm

build/srd_pipeline_tests: tests/srd_pipeline_tests.c $(SRD2_C_OBJS) $(SRD2_SVO_DEPS) | build
	$(CC) $(SRD2_CFLAGS) $< $(SRD2_C_OBJS) $(SRD2_SVO_DEPS) -o $@ -lm

build/srd_raycast_tests: tests/srd_raycast_tests.c $(SRD2_C_OBJS) $(SRD2_SVO_DEPS) | build
	$(CC) $(SRD2_CFLAGS) $< $(SRD2_C_OBJS) $(SRD2_SVO_DEPS) -o $@ -lm

build/srd_sdf_grid_tests: tests/srd_sdf_grid_tests.c $(SRD2_C_OBJS) $(SRD2_SVO_DEPS) | build
	$(CC) $(SRD2_CFLAGS) $< $(SRD2_C_OBJS) $(SRD2_SVO_DEPS) -o $@ -lm

build/srd_room_map_tests: tests/srd_room_map_tests.c $(SRD2_C_OBJS) $(SRD2_SVO_DEPS) | build
	$(CC) $(SRD2_CFLAGS) $< $(SRD2_C_OBJS) $(SRD2_SVO_DEPS) -o $@ -lm

build/srd_seed_init_tests: tests/srd_seed_init_tests.c $(SRD2_C_OBJS) $(SRD2_SVO_DEPS) | build
	$(CC) $(SRD2_CFLAGS) $< $(SRD2_C_OBJS) $(SRD2_SVO_DEPS) -o $@ -lm

build/srd_sdf_to_svo_tests: tests/srd_sdf_to_svo_tests.c $(SRD2_C_OBJS) $(SRD2_SVO_DEPS) | build
	$(CC) $(SRD2_CFLAGS) $< $(SRD2_C_OBJS) $(SRD2_SVO_DEPS) -o $@ -lm

build/srd_grid_critic_tests: tests/srd_grid_critic_tests.c $(SRD2_C_OBJS) $(SRD2_SVO_DEPS) | build
	$(CC) $(SRD2_CFLAGS) $< $(SRD2_C_OBJS) $(SRD2_SVO_DEPS) -o $@ -lm

build/srd_rules_wall_tests: tests/srd_rules_wall_tests.c $(SRD2_C_OBJS) $(SRD2_SVO_DEPS) | build
	$(CC) $(SRD2_CFLAGS) $< $(SRD2_C_OBJS) $(SRD2_SVO_DEPS) -o $@ -lm

build/srd_rules_corner_tests: tests/srd_rules_corner_tests.c $(SRD2_C_OBJS) $(SRD2_SVO_DEPS) | build
	$(CC) $(SRD2_CFLAGS) $< $(SRD2_C_OBJS) $(SRD2_SVO_DEPS) -o $@ -lm

build/srd_rules_height_tests: tests/srd_rules_height_tests.c $(SRD2_C_OBJS) $(SRD2_SVO_DEPS) | build
	$(CC) $(SRD2_CFLAGS) $< $(SRD2_C_OBJS) $(SRD2_SVO_DEPS) -o $@ -lm

build/srd_rules_vcorridor_tests: tests/srd_rules_vcorridor_tests.c $(SRD2_C_OBJS) $(SRD2_SVO_DEPS) | build
	$(CC) $(SRD2_CFLAGS) $< $(SRD2_C_OBJS) $(SRD2_SVO_DEPS) -o $@ -lm

build/srd_rules_vfeature_tests: tests/srd_rules_vfeature_tests.c $(SRD2_C_OBJS) $(SRD2_SVO_DEPS) | build
	$(CC) $(SRD2_CFLAGS) $< $(SRD2_C_OBJS) $(SRD2_SVO_DEPS) -o $@ -lm

build/srd_rules_embellish_tests: tests/srd_rules_embellish_tests.c $(SRD2_C_OBJS) $(SRD2_SVO_DEPS) | build
	$(CC) $(SRD2_CFLAGS) $< $(SRD2_C_OBJS) $(SRD2_SVO_DEPS) -o $@ -lm

SRD2_TESTS := build/srd_rules_embellish_tests \
              build/srd_rules_vfeature_tests \
              build/srd_rules_vcorridor_tests \
              build/srd_rules_height_tests \
              build/srd_rules_corner_tests \
              build/srd_rules_wall_tests \
              build/srd_grid_critic_tests \
              build/srd_sdf_grid_tests \
              build/srd_room_map_tests \
              build/srd_seed_init_tests \
              build/srd_sdf_to_svo_tests \
              build/srd_descent_loop_tests \
              build/srd_bridge_tests \
              build/srd_property_tests \
              build/srd_critic_value_tests \
              build/srd_pipeline_tests \
              build/srd_raycast_tests

srd2-test: $(SRD2_TESTS)
	@for t in $(SRD2_TESTS); do echo "Running $$t..."; ./$$t || exit 1; done
	@echo "All SRD v2 tests passed."

PROCGEN_TESTS += $(SRD2_TESTS)

clean: clean-procgen
clean-procgen:
	$(RM) $(PROCGEN_TESTS)
	$(RM) -r /tmp/srd_codegen

###########################

clean:
	$(RM) $(BIN) $(PROCGEN_TESTS) build/libheadless.a build/liball.a build/demo_server build/demo_client build/editor_tui
	$(RM) -r build/obj

build/p004_visual_texture: tests/visual/p004_visual_texture.c src/renderer/texture_create.c src/renderer/texture_upload.c src/renderer/texture_bind.c src/renderer/shader_program_create.c src/renderer/shader_program_bind.c src/renderer/shader_program_destroy.c src/renderer/shader_program_handle.c src/renderer/gl_loader_validate.c $(OBJ_GLAD) | build
	$(CC) $(CFLAGS) $(RENDERER_TEST_CFLAGS) tests/visual/p004_visual_texture.c src/renderer/texture_create.c src/renderer/texture_upload.c src/renderer/texture_bind.c src/renderer/shader_program_create.c src/renderer/shader_program_bind.c src/renderer/shader_program_destroy.c src/renderer/shader_program_handle.c src/renderer/gl_loader_validate.c $(OBJ_GLAD) -o $@ $(RENDERER_TEST_LIBS) -ldl -lm

build/p004_material_tests: tests/p004_renderer_material_tests.c src/renderer/material.c src/renderer/texture_create.c src/renderer/texture_upload.c src/renderer/texture_bind.c src/renderer/shader_program_create.c src/renderer/shader_program_bind.c src/renderer/shader_program_destroy.c src/renderer/shader_program_handle.c src/renderer/shader_uniforms_init.c src/renderer/gl_loader_validate.c $(OBJ_GLAD) | build
	$(CC) $(CFLAGS) $(RENDERER_TEST_CFLAGS) tests/p004_renderer_material_tests.c src/renderer/material.c src/renderer/texture_create.c src/renderer/texture_upload.c src/renderer/texture_bind.c src/renderer/shader_program_create.c src/renderer/shader_program_bind.c src/renderer/shader_program_destroy.c src/renderer/shader_program_handle.c src/renderer/shader_uniforms_init.c src/renderer/gl_loader_validate.c $(OBJ_GLAD) -o $@ $(RENDERER_TEST_LIBS) -ldl -lm

build/p004_visual_material: tests/visual/p004_visual_material.c src/renderer/material.c src/renderer/texture_create.c src/renderer/texture_upload.c src/renderer/texture_bind.c src/renderer/shader_program_create.c src/renderer/shader_program_bind.c src/renderer/shader_program_destroy.c src/renderer/shader_program_handle.c src/renderer/shader_uniforms_init.c src/renderer/gl_loader_validate.c $(OBJ_GLAD) | build
	$(CC) $(CFLAGS) $(RENDERER_TEST_CFLAGS) tests/visual/p004_visual_material.c src/renderer/material.c src/renderer/texture_create.c src/renderer/texture_upload.c src/renderer/texture_bind.c src/renderer/shader_program_create.c src/renderer/shader_program_bind.c src/renderer/shader_program_destroy.c src/renderer/shader_program_handle.c src/renderer/shader_uniforms_init.c src/renderer/gl_loader_validate.c $(OBJ_GLAD) -o $@ $(RENDERER_TEST_LIBS) -ldl -lm

build/p004_pbr_shader_tests: tests/p004_renderer_pbr_shader_tests.c src/renderer/pbr_shader.c src/renderer/material.c src/renderer/texture_create.c src/renderer/texture_upload.c src/renderer/texture_bind.c src/renderer/shader_program_create.c src/renderer/shader_program_bind.c src/renderer/shader_program_destroy.c src/renderer/shader_program_handle.c src/renderer/shader_uniforms_init.c src/renderer/gl_loader_validate.c $(OBJ_GLAD) | build
	$(CC) $(CFLAGS) $(RENDERER_TEST_CFLAGS) tests/p004_renderer_pbr_shader_tests.c src/renderer/pbr_shader.c src/renderer/material.c src/renderer/texture_create.c src/renderer/texture_upload.c src/renderer/texture_bind.c src/renderer/shader_program_create.c src/renderer/shader_program_bind.c src/renderer/shader_program_destroy.c src/renderer/shader_program_handle.c src/renderer/shader_uniforms_init.c src/renderer/gl_loader_validate.c $(OBJ_GLAD) -o $@ $(RENDERER_TEST_LIBS) -ldl -lm

build/p004_visual_pbr_spheres: tests/visual/p004_visual_pbr_spheres.c src/renderer/pbr_shader.c src/renderer/material.c src/renderer/texture_create.c src/renderer/texture_upload.c src/renderer/texture_bind.c src/renderer/shader_program_create.c src/renderer/shader_program_bind.c src/renderer/shader_program_destroy.c src/renderer/shader_program_handle.c src/renderer/shader_uniforms_init.c src/renderer/gl_loader_validate.c src/math/mat4_proj.c src/math/mat4_look_at.c src/math/mat4_mul.c src/math/mat4_basic.c src/math/vec3.c $(OBJ_GLAD) | build
	$(CC) $(CFLAGS) $(RENDERER_TEST_CFLAGS) tests/visual/p004_visual_pbr_spheres.c src/renderer/pbr_shader.c src/renderer/material.c src/renderer/texture_create.c src/renderer/texture_upload.c src/renderer/texture_bind.c src/renderer/shader_program_create.c src/renderer/shader_program_bind.c src/renderer/shader_program_destroy.c src/renderer/shader_program_handle.c src/renderer/shader_uniforms_init.c src/renderer/gl_loader_validate.c src/math/mat4_proj.c src/math/mat4_look_at.c src/math/mat4_mul.c src/math/mat4_basic.c src/math/vec3.c $(OBJ_GLAD) -o $@ $(RENDERER_TEST_LIBS) -ldl -lm

build/p004_visual_sh_lightmap: tests/visual/p004_visual_sh_lightmap.c src/renderer/pbr_shader.c src/renderer/material.c src/renderer/texture_create.c src/renderer/texture_upload.c src/renderer/texture_bind.c src/renderer/shader_program_create.c src/renderer/shader_program_bind.c src/renderer/shader_program_destroy.c src/renderer/shader_program_handle.c src/renderer/shader_uniforms_init.c src/renderer/gl_loader_validate.c build/libheadless.a $(OBJ_GLAD) | build
	$(CC) $(CFLAGS) $(RENDERER_TEST_CFLAGS) tests/visual/p004_visual_sh_lightmap.c src/renderer/pbr_shader.c src/renderer/material.c src/renderer/texture_create.c src/renderer/texture_upload.c src/renderer/texture_bind.c src/renderer/shader_program_create.c src/renderer/shader_program_bind.c src/renderer/shader_program_destroy.c src/renderer/shader_program_handle.c src/renderer/shader_uniforms_init.c src/renderer/gl_loader_validate.c build/libheadless.a $(OBJ_GLAD) -o $@ $(RENDERER_TEST_LIBS) -ldl -lm

build/p004_visual_pbr_lights: tests/visual/p004_visual_pbr_lights.c src/renderer/pbr_shader.c src/renderer/material.c src/renderer/texture_create.c src/renderer/texture_upload.c src/renderer/texture_bind.c src/renderer/shader_program_create.c src/renderer/shader_program_bind.c src/renderer/shader_program_destroy.c src/renderer/shader_program_handle.c src/renderer/shader_uniforms_init.c src/renderer/gl_loader_validate.c src/math/mat4_proj.c src/math/mat4_look_at.c src/math/mat4_mul.c src/math/mat4_basic.c src/math/vec3.c $(OBJ_GLAD) | build
	$(CC) $(CFLAGS) $(RENDERER_TEST_CFLAGS) tests/visual/p004_visual_pbr_lights.c src/renderer/pbr_shader.c src/renderer/material.c src/renderer/texture_create.c src/renderer/texture_upload.c src/renderer/texture_bind.c src/renderer/shader_program_create.c src/renderer/shader_program_bind.c src/renderer/shader_program_destroy.c src/renderer/shader_program_handle.c src/renderer/shader_uniforms_init.c src/renderer/gl_loader_validate.c src/math/mat4_proj.c src/math/mat4_look_at.c src/math/mat4_mul.c src/math/mat4_basic.c src/math/vec3.c $(OBJ_GLAD) -o $@ $(RENDERER_TEST_LIBS) -ldl -lm

build/p004_visual_pbr_material: tests/visual/p004_visual_pbr_material.c src/renderer/pbr_shader.c src/renderer/material.c src/renderer/texture_create.c src/renderer/texture_upload.c src/renderer/texture_bind.c src/renderer/shader_program_create.c src/renderer/shader_program_bind.c src/renderer/shader_program_destroy.c src/renderer/shader_program_handle.c src/renderer/shader_uniforms_init.c src/renderer/gl_loader_validate.c src/math/mat4_proj.c src/math/mat4_look_at.c src/math/mat4_mul.c src/math/mat4_basic.c src/math/vec3.c $(OBJ_GLAD) | build
	$(CC) $(CFLAGS) $(RENDERER_TEST_CFLAGS) tests/visual/p004_visual_pbr_material.c src/renderer/pbr_shader.c src/renderer/material.c src/renderer/texture_create.c src/renderer/texture_upload.c src/renderer/texture_bind.c src/renderer/shader_program_create.c src/renderer/shader_program_bind.c src/renderer/shader_program_destroy.c src/renderer/shader_program_handle.c src/renderer/shader_uniforms_init.c src/renderer/gl_loader_validate.c src/math/mat4_proj.c src/math/mat4_look_at.c src/math/mat4_mul.c src/math/mat4_basic.c src/math/vec3.c $(OBJ_GLAD) -o $@ $(RENDERER_TEST_LIBS) -ldl -lm

build/p004_light_store_tests: tests/p004_renderer_light_store_tests.c src/renderer/light_store.c | build
	$(CC) $(CFLAGS) tests/p004_renderer_light_store_tests.c src/renderer/light_store.c -o $@ -lm

build/p004_visual_light_store: tests/visual/p004_visual_light_store.c src/renderer/pbr_shader.c src/renderer/material.c src/renderer/light_store.c src/renderer/texture_create.c src/renderer/texture_upload.c src/renderer/texture_bind.c src/renderer/shader_program_create.c src/renderer/shader_program_bind.c src/renderer/shader_program_destroy.c src/renderer/shader_program_handle.c src/renderer/shader_uniforms_init.c src/renderer/gl_loader_validate.c src/math/mat4_proj.c src/math/mat4_look_at.c src/math/mat4_mul.c src/math/mat4_basic.c src/math/vec3.c $(OBJ_GLAD) | build
	$(CC) $(CFLAGS) $(RENDERER_TEST_CFLAGS) tests/visual/p004_visual_light_store.c src/renderer/pbr_shader.c src/renderer/material.c src/renderer/light_store.c src/renderer/texture_create.c src/renderer/texture_upload.c src/renderer/texture_bind.c src/renderer/shader_program_create.c src/renderer/shader_program_bind.c src/renderer/shader_program_destroy.c src/renderer/shader_program_handle.c src/renderer/shader_uniforms_init.c src/renderer/gl_loader_validate.c src/math/mat4_proj.c src/math/mat4_look_at.c src/math/mat4_mul.c src/math/mat4_basic.c src/math/vec3.c $(OBJ_GLAD) -o $@ $(RENDERER_TEST_LIBS) -ldl -lm

build/p004_scene_tests: tests/p004_renderer_scene_tests.c src/renderer/render_scene.c src/renderer/render_camera.c src/renderer/light_store.c src/math/mat4_proj.c src/math/mat4_look_at.c src/math/mat4_mul.c src/math/mat4_basic.c src/math/vec3.c | build
	$(CC) $(CFLAGS) tests/p004_renderer_scene_tests.c src/renderer/render_scene.c src/renderer/render_camera.c src/renderer/light_store.c src/math/mat4_proj.c src/math/mat4_look_at.c src/math/mat4_mul.c src/math/mat4_basic.c src/math/vec3.c -o $@ -lm

build/p004_gpu_cmd_queue_tests: tests/p004_renderer_gpu_cmd_queue_tests.c src/renderer/resource/gpu_cmd_queue.c | build
	$(CC) $(CFLAGS) tests/p004_renderer_gpu_cmd_queue_tests.c src/renderer/resource/gpu_cmd_queue.c -o $@ -lpthread

build/p004_gpu_registry_tests: tests/p004_renderer_gpu_registry_tests.c src/renderer/resource/gpu_registry_create.c src/renderer/resource/gpu_registry_access.c src/memory/pool_alloc.c src/memory/pool_destroy.c src/memory/pool_free.c src/memory/pool_get.c src/memory/pool_init.c | build
	$(CC) $(CFLAGS) tests/p004_renderer_gpu_registry_tests.c src/renderer/resource/gpu_registry_create.c src/renderer/resource/gpu_registry_access.c src/memory/pool_alloc.c src/memory/pool_destroy.c src/memory/pool_free.c src/memory/pool_get.c src/memory/pool_init.c -o $@ -lpthread

build/p004_shadow_slotmap_tests: tests/p004_renderer_shadow_slotmap_tests.c src/renderer/resource/shadow_slotmap.c | build
	$(CC) $(CFLAGS) tests/p004_renderer_shadow_slotmap_tests.c src/renderer/resource/shadow_slotmap.c -o $@

build/p004_visual_scene: tests/visual/p004_visual_scene.c src/renderer/render_scene.c src/renderer/render_camera.c src/renderer/light_store.c src/renderer/mesh/static_mesh_create.c src/renderer/mesh/static_mesh_destroy.c src/renderer/mesh/static_mesh_draw.c src/renderer/mesh/static_mesh_primitives.c src/renderer/vao_create.c src/renderer/vao_bind_attributes.c src/renderer/vao_destroy.c src/renderer/vbo_create.c src/renderer/vbo_upload.c src/renderer/vbo_destroy.c src/renderer/pbr_shader.c src/renderer/material.c src/renderer/texture_create.c src/renderer/texture_upload.c src/renderer/texture_bind.c src/renderer/shader_program_create.c src/renderer/shader_program_bind.c src/renderer/shader_program_destroy.c src/renderer/shader_program_handle.c src/renderer/shader_uniforms_init.c src/renderer/gl_loader_validate.c src/math/mat4_proj.c src/math/mat4_look_at.c src/math/mat4_mul.c src/math/mat4_basic.c src/math/vec3.c $(OBJ_GLAD) | build
	$(CC) $(CFLAGS) $(RENDERER_TEST_CFLAGS) tests/visual/p004_visual_scene.c src/renderer/render_scene.c src/renderer/render_camera.c src/renderer/light_store.c src/renderer/mesh/static_mesh_create.c src/renderer/mesh/static_mesh_destroy.c src/renderer/mesh/static_mesh_draw.c src/renderer/mesh/static_mesh_primitives.c src/renderer/vao_create.c src/renderer/vao_bind_attributes.c src/renderer/vao_destroy.c src/renderer/vbo_create.c src/renderer/vbo_upload.c src/renderer/vbo_destroy.c src/renderer/pbr_shader.c src/renderer/material.c src/renderer/texture_create.c src/renderer/texture_upload.c src/renderer/texture_bind.c src/renderer/shader_program_create.c src/renderer/shader_program_bind.c src/renderer/shader_program_destroy.c src/renderer/shader_program_handle.c src/renderer/shader_uniforms_init.c src/renderer/gl_loader_validate.c src/math/mat4_proj.c src/math/mat4_look_at.c src/math/mat4_mul.c src/math/mat4_basic.c src/math/vec3.c $(OBJ_GLAD) -o $@ $(RENDERER_TEST_LIBS) -ldl -lm

build/p004_depth_prepass_tests: tests/p004_renderer_depth_prepass_tests.c src/renderer/depth_prepass.c src/renderer/render_scene.c src/renderer/render_camera.c src/renderer/mesh/static_mesh_create.c src/renderer/mesh/static_mesh_destroy.c src/renderer/mesh/static_mesh_draw.c src/renderer/mesh/static_mesh_primitives.c src/renderer/vao_create.c src/renderer/vao_bind_attributes.c src/renderer/vao_destroy.c src/renderer/vbo_create.c src/renderer/vbo_upload.c src/renderer/vbo_destroy.c src/renderer/material.c src/renderer/texture_create.c src/renderer/texture_upload.c src/renderer/texture_bind.c src/renderer/shader_program_create.c src/renderer/shader_program_bind.c src/renderer/shader_program_destroy.c src/renderer/shader_program_handle.c src/renderer/shader_uniforms_init.c src/renderer/gl_loader_validate.c src/math/mat4_proj.c src/math/mat4_look_at.c src/math/mat4_mul.c src/math/mat4_basic.c src/math/vec3.c $(OBJ_GLAD) | build
	$(CC) $(CFLAGS) $(RENDERER_TEST_CFLAGS) tests/p004_renderer_depth_prepass_tests.c src/renderer/depth_prepass.c src/renderer/render_scene.c src/renderer/render_camera.c src/renderer/mesh/static_mesh_create.c src/renderer/mesh/static_mesh_destroy.c src/renderer/mesh/static_mesh_draw.c src/renderer/mesh/static_mesh_primitives.c src/renderer/vao_create.c src/renderer/vao_bind_attributes.c src/renderer/vao_destroy.c src/renderer/vbo_create.c src/renderer/vbo_upload.c src/renderer/vbo_destroy.c src/renderer/material.c src/renderer/texture_create.c src/renderer/texture_upload.c src/renderer/texture_bind.c src/renderer/shader_program_create.c src/renderer/shader_program_bind.c src/renderer/shader_program_destroy.c src/renderer/shader_program_handle.c src/renderer/shader_uniforms_init.c src/renderer/gl_loader_validate.c src/math/mat4_proj.c src/math/mat4_look_at.c src/math/mat4_mul.c src/math/mat4_basic.c src/math/vec3.c $(OBJ_GLAD) -o $@ $(RENDERER_TEST_LIBS) -ldl -lm

build/p004_visual_depth_prepass: tests/visual/p004_visual_depth_prepass.c src/renderer/depth_prepass.c src/renderer/render_scene.c src/renderer/render_camera.c src/renderer/mesh/static_mesh_create.c src/renderer/mesh/static_mesh_destroy.c src/renderer/mesh/static_mesh_draw.c src/renderer/mesh/static_mesh_primitives.c src/renderer/vao_create.c src/renderer/vao_bind_attributes.c src/renderer/vao_destroy.c src/renderer/vbo_create.c src/renderer/vbo_upload.c src/renderer/vbo_destroy.c src/renderer/material.c src/renderer/texture_create.c src/renderer/texture_upload.c src/renderer/texture_bind.c src/renderer/shader_program_create.c src/renderer/shader_program_bind.c src/renderer/shader_program_destroy.c src/renderer/shader_program_handle.c src/renderer/shader_uniforms_init.c src/renderer/gl_loader_validate.c src/math/mat4_proj.c src/math/mat4_look_at.c src/math/mat4_mul.c src/math/mat4_basic.c src/math/vec3.c $(OBJ_GLAD) | build
	$(CC) $(CFLAGS) $(RENDERER_TEST_CFLAGS) tests/visual/p004_visual_depth_prepass.c src/renderer/depth_prepass.c src/renderer/render_scene.c src/renderer/render_camera.c src/renderer/mesh/static_mesh_create.c src/renderer/mesh/static_mesh_destroy.c src/renderer/mesh/static_mesh_draw.c src/renderer/mesh/static_mesh_primitives.c src/renderer/vao_create.c src/renderer/vao_bind_attributes.c src/renderer/vao_destroy.c src/renderer/vbo_create.c src/renderer/vbo_upload.c src/renderer/vbo_destroy.c src/renderer/material.c src/renderer/texture_create.c src/renderer/texture_upload.c src/renderer/texture_bind.c src/renderer/shader_program_create.c src/renderer/shader_program_bind.c src/renderer/shader_program_destroy.c src/renderer/shader_program_handle.c src/renderer/shader_uniforms_init.c src/renderer/gl_loader_validate.c src/math/mat4_proj.c src/math/mat4_look_at.c src/math/mat4_mul.c src/math/mat4_basic.c src/math/vec3.c $(OBJ_GLAD) -o $@ $(RENDERER_TEST_LIBS) -ldl -lm

build/p004_cluster_tests: tests/p004_renderer_cluster_tests.c src/renderer/cluster_grid.c src/renderer/render_camera.c src/math/mat4_proj.c src/math/mat4_look_at.c src/math/mat4_mul.c src/math/mat4_basic.c src/math/vec3.c | build
	$(CC) $(CFLAGS) tests/p004_renderer_cluster_tests.c src/renderer/cluster_grid.c src/renderer/render_camera.c src/math/mat4_proj.c src/math/mat4_look_at.c src/math/mat4_mul.c src/math/mat4_basic.c src/math/vec3.c -o $@ -lm

build/p004_visual_cluster_heatmap: tests/visual/p004_visual_cluster_heatmap.c src/renderer/cluster_grid.c src/renderer/render_camera.c src/renderer/shader_program_create.c src/renderer/shader_program_bind.c src/renderer/shader_program_destroy.c src/renderer/shader_program_handle.c src/renderer/gl_loader_validate.c src/math/mat4_proj.c src/math/mat4_look_at.c src/math/mat4_mul.c src/math/mat4_basic.c src/math/vec3.c $(OBJ_GLAD) | build
	$(CC) $(CFLAGS) $(RENDERER_TEST_CFLAGS) tests/visual/p004_visual_cluster_heatmap.c src/renderer/cluster_grid.c src/renderer/render_camera.c src/renderer/shader_program_create.c src/renderer/shader_program_bind.c src/renderer/shader_program_destroy.c src/renderer/shader_program_handle.c src/renderer/gl_loader_validate.c src/math/mat4_proj.c src/math/mat4_look_at.c src/math/mat4_mul.c src/math/mat4_basic.c src/math/vec3.c $(OBJ_GLAD) -o $@ $(RENDERER_TEST_LIBS) -ldl -lm

build/obj_mesh_load_tests: tests/mesh/obj_mesh_load_tests.c src/mesh/obj_mesh_load.c | build
	$(CC) $(CFLAGS) -D_POSIX_C_SOURCE=200809L tests/mesh/obj_mesh_load_tests.c src/mesh/obj_mesh_load.c -o $@ -lm

build/dmesh_load_tests: tests/mesh/dmesh_load_tests.c src/mesh/dmesh_load.c src/mesh/obj_mesh_load.c include/ferrum/mesh/dmesh_loader.h | build
	$(CC) $(CFLAGS) tests/mesh/dmesh_load_tests.c src/mesh/dmesh_load.c src/mesh/obj_mesh_load.c -o $@ -lm

build/p004_visual_hall_forward: tests/visual/p004_visual_hall_forward.c src/renderer/forward_plus.c src/renderer/cluster_grid.c src/renderer/render_camera.c src/renderer/render_scene.c src/renderer/light_store.c src/mesh/obj_mesh_load.c src/renderer/mesh/static_mesh_create.c src/renderer/mesh/static_mesh_destroy.c src/renderer/mesh/static_mesh_draw.c src/renderer/vao_create.c src/renderer/vao_bind_attributes.c src/renderer/vao_destroy.c src/renderer/vbo_create.c src/renderer/vbo_upload.c src/renderer/vbo_destroy.c src/renderer/pbr_shader.c src/renderer/material.c src/renderer/texture_create.c src/renderer/texture_upload.c src/renderer/texture_bind.c src/renderer/shader_program_create.c src/renderer/shader_program_bind.c src/renderer/shader_program_destroy.c src/renderer/shader_program_handle.c src/renderer/shader_uniforms_init.c src/renderer/gl_loader_validate.c src/math/mat4_proj.c src/math/mat4_look_at.c src/math/mat4_mul.c src/math/mat4_basic.c src/math/vec3.c $(OBJ_GLAD) | build
	$(CC) $(CFLAGS) -D_POSIX_C_SOURCE=200809L $(RENDERER_TEST_CFLAGS) tests/visual/p004_visual_hall_forward.c src/renderer/forward_plus.c src/renderer/cluster_grid.c src/renderer/render_camera.c src/renderer/render_scene.c src/renderer/light_store.c src/mesh/obj_mesh_load.c src/renderer/mesh/static_mesh_create.c src/renderer/mesh/static_mesh_destroy.c src/renderer/mesh/static_mesh_draw.c src/renderer/vao_create.c src/renderer/vao_bind_attributes.c src/renderer/vao_destroy.c src/renderer/vbo_create.c src/renderer/vbo_upload.c src/renderer/vbo_destroy.c src/renderer/pbr_shader.c src/renderer/material.c src/renderer/texture_create.c src/renderer/texture_upload.c src/renderer/texture_bind.c src/renderer/shader_program_create.c src/renderer/shader_program_bind.c src/renderer/shader_program_destroy.c src/renderer/shader_program_handle.c src/renderer/shader_uniforms_init.c src/renderer/gl_loader_validate.c src/math/mat4_proj.c src/math/mat4_look_at.c src/math/mat4_mul.c src/math/mat4_basic.c src/math/vec3.c $(OBJ_GLAD) -o $@ $(RENDERER_TEST_LIBS) -ldl -lm

build/lm_mesh_luxel_tests: tests/lightmap/lm_mesh_luxel_tests.c src/lightmap/lm_mesh_luxel.c src/lightmap/lm_image.c src/lightmap/lm_sh.c src/math/vec3.c include/ferrum/lightmap/lm_mesh_luxel.h | build
	$(CC) $(CFLAGS) tests/lightmap/lm_mesh_luxel_tests.c src/lightmap/lm_mesh_luxel.c src/lightmap/lm_image.c src/lightmap/lm_sh.c src/math/vec3.c -o $@ -lm

build/lm_mesh_bake_tests: tests/lightmap/lm_mesh_bake_tests.c src/lightmap/lm_material.c src/lightmap/lm_mesh_bake.c src/lightmap/lm_svo_voxelize.c src/lightmap/lm_gi_gather.c src/lightmap/lm_parallel.c src/lightmap/lm_mesh_luxel.c src/lightmap/lm_image.c src/lightmap/lm_farfield.c src/lightmap/lm_sky.c src/lightmap/lm_svo_mip.c src/lightmap/lm_svo_material.c src/lightmap/lm_kdtree.c src/lightmap/lm_solve.c src/lightmap/lm_visibility.c src/lightmap/lm_light.c src/lightmap/lm_atlas.c src/lightmap/lm_lightmap.c src/lightmap/lm_sh.c src/math/vec3.c src/memory/arena_init.c src/memory/arena_alloc.c src/memory/arena_mark.c src/memory/arena_pop.c src/npc/nav/npc_svo_init.c src/npc/nav/npc_svo_rasterize.c src/npc/nav/npc_svo_blocker.c include/ferrum/lightmap/lm_mesh_bake.h | build
	$(CC) $(CFLAGS) tests/lightmap/lm_mesh_bake_tests.c src/lightmap/lm_material.c src/lightmap/lm_mesh_bake.c src/lightmap/lm_svo_voxelize.c src/lightmap/lm_gi_gather.c src/lightmap/lm_parallel.c src/lightmap/lm_mesh_luxel.c src/lightmap/lm_image.c src/lightmap/lm_farfield.c src/lightmap/lm_sky.c src/lightmap/lm_svo_mip.c src/lightmap/lm_svo_material.c src/lightmap/lm_kdtree.c src/lightmap/lm_solve.c src/lightmap/lm_visibility.c src/lightmap/lm_light.c src/lightmap/lm_atlas.c src/lightmap/lm_lightmap.c src/lightmap/lm_sh.c src/math/vec3.c src/memory/arena_init.c src/memory/arena_alloc.c src/memory/arena_mark.c src/memory/arena_pop.c src/npc/nav/npc_svo_init.c src/npc/nav/npc_svo_rasterize.c src/npc/nav/npc_svo_blocker.c -o $@ -lm

build/lm_cornell_visual: tests/visual/lm_cornell_visual.c src/lightmap/lm_material.c src/lightmap/lm_mesh_bake.c src/lightmap/lm_svo_voxelize.c src/lightmap/lm_gi_gather.c src/lightmap/lm_parallel.c src/lightmap/lm_mesh_luxel.c src/lightmap/lm_image.c src/lightmap/lm_farfield.c src/lightmap/lm_sky.c src/lightmap/lm_svo_mip.c src/lightmap/lm_svo_material.c src/lightmap/lm_kdtree.c src/lightmap/lm_solve.c src/lightmap/lm_visibility.c src/lightmap/lm_light.c src/lightmap/lm_atlas.c src/lightmap/lm_lightmap.c src/lightmap/lm_sh.c src/memory/arena_init.c src/memory/arena_alloc.c src/memory/arena_mark.c src/memory/arena_pop.c src/npc/nav/npc_svo_init.c src/npc/nav/npc_svo_rasterize.c src/npc/nav/npc_svo_blocker.c src/renderer/pbr_shader.c src/renderer/material.c src/renderer/texture_create.c src/renderer/texture_upload.c src/renderer/texture_bind.c src/renderer/shader_program_create.c src/renderer/shader_program_bind.c src/renderer/shader_program_destroy.c src/renderer/shader_program_handle.c src/renderer/shader_uniforms_init.c src/renderer/gl_loader_validate.c src/math/mat4_proj.c src/math/mat4_look_at.c src/math/mat4_mul.c src/math/mat4_basic.c src/math/vec3.c $(OBJ_GLAD) | build
	$(CC) $(CFLAGS) $(RENDERER_TEST_CFLAGS) tests/visual/lm_cornell_visual.c src/lightmap/lm_material.c src/lightmap/lm_mesh_bake.c src/lightmap/lm_svo_voxelize.c src/lightmap/lm_gi_gather.c src/lightmap/lm_parallel.c src/lightmap/lm_mesh_luxel.c src/lightmap/lm_image.c src/lightmap/lm_farfield.c src/lightmap/lm_sky.c src/lightmap/lm_svo_mip.c src/lightmap/lm_svo_material.c src/lightmap/lm_kdtree.c src/lightmap/lm_solve.c src/lightmap/lm_visibility.c src/lightmap/lm_light.c src/lightmap/lm_atlas.c src/lightmap/lm_lightmap.c src/lightmap/lm_sh.c src/memory/arena_init.c src/memory/arena_alloc.c src/memory/arena_mark.c src/memory/arena_pop.c src/npc/nav/npc_svo_init.c src/npc/nav/npc_svo_rasterize.c src/npc/nav/npc_svo_blocker.c src/renderer/pbr_shader.c src/renderer/material.c src/renderer/texture_create.c src/renderer/texture_upload.c src/renderer/texture_bind.c src/renderer/shader_program_create.c src/renderer/shader_program_bind.c src/renderer/shader_program_destroy.c src/renderer/shader_program_handle.c src/renderer/shader_uniforms_init.c src/renderer/gl_loader_validate.c src/math/mat4_proj.c src/math/mat4_look_at.c src/math/mat4_mul.c src/math/mat4_basic.c src/math/vec3.c $(OBJ_GLAD) -o $@ $(RENDERER_TEST_LIBS) -ldl -lm

build/lm_lightmap_file_tests: tests/lightmap/lm_lightmap_file_tests.c src/lightmap/lm_lightmap_file.c src/lightmap/lm_material.c src/lightmap/lm_mesh_bake.c src/lightmap/lm_svo_voxelize.c src/lightmap/lm_gi_gather.c src/lightmap/lm_parallel.c src/lightmap/lm_mesh_luxel.c src/lightmap/lm_image.c src/lightmap/lm_farfield.c src/lightmap/lm_sky.c src/lightmap/lm_svo_mip.c src/lightmap/lm_svo_material.c src/lightmap/lm_kdtree.c src/lightmap/lm_solve.c src/lightmap/lm_visibility.c src/lightmap/lm_light.c src/lightmap/lm_atlas.c src/lightmap/lm_lightmap.c src/lightmap/lm_sh.c src/math/vec3.c src/memory/arena_init.c src/memory/arena_alloc.c src/memory/arena_mark.c src/memory/arena_pop.c src/npc/nav/npc_svo_init.c src/npc/nav/npc_svo_rasterize.c src/npc/nav/npc_svo_blocker.c | build
	$(CC) $(CFLAGS) tests/lightmap/lm_lightmap_file_tests.c src/lightmap/lm_lightmap_file.c src/lightmap/lm_material.c src/lightmap/lm_mesh_bake.c src/lightmap/lm_svo_voxelize.c src/lightmap/lm_gi_gather.c src/lightmap/lm_parallel.c src/lightmap/lm_mesh_luxel.c src/lightmap/lm_image.c src/lightmap/lm_farfield.c src/lightmap/lm_sky.c src/lightmap/lm_svo_mip.c src/lightmap/lm_svo_material.c src/lightmap/lm_kdtree.c src/lightmap/lm_solve.c src/lightmap/lm_visibility.c src/lightmap/lm_light.c src/lightmap/lm_atlas.c src/lightmap/lm_lightmap.c src/lightmap/lm_sh.c src/math/vec3.c src/memory/arena_init.c src/memory/arena_alloc.c src/memory/arena_mark.c src/memory/arena_pop.c src/npc/nav/npc_svo_init.c src/npc/nav/npc_svo_rasterize.c src/npc/nav/npc_svo_blocker.c -o $@ -lm

build/hall_bake_render: tests/visual/hall_bake_render.c src/mesh/dmesh_load.c src/mesh/obj_mesh_load.c src/lightmap/lm_lightmap_file.c src/lightmap/lm_material.c src/lightmap/lm_mesh_bake.c src/lightmap/lm_svo_voxelize.c src/lightmap/lm_gi_gather.c src/lightmap/lm_parallel.c src/lightmap/lm_mesh_luxel.c src/lightmap/lm_image.c src/lightmap/lm_farfield.c src/lightmap/lm_sky.c src/lightmap/lm_svo_mip.c src/lightmap/lm_svo_material.c src/lightmap/lm_kdtree.c src/lightmap/lm_solve.c src/lightmap/lm_visibility.c src/lightmap/lm_light.c src/lightmap/lm_atlas.c src/lightmap/lm_lightmap.c src/lightmap/lm_sh.c src/memory/arena_init.c src/memory/arena_alloc.c src/memory/arena_mark.c src/memory/arena_pop.c src/npc/nav/npc_svo_init.c src/npc/nav/npc_svo_rasterize.c src/npc/nav/npc_svo_blocker.c src/renderer/forward_plus.c src/renderer/cluster_grid.c src/renderer/render_camera.c src/renderer/pbr_shader.c src/renderer/material.c src/renderer/texture_create.c src/renderer/texture_upload.c src/renderer/texture_bind.c src/renderer/shader_program_create.c src/renderer/shader_program_bind.c src/renderer/shader_program_destroy.c src/renderer/shader_program_handle.c src/renderer/shader_uniforms_init.c src/renderer/gl_loader_validate.c src/math/mat4_proj.c src/math/mat4_look_at.c src/math/mat4_mul.c src/math/mat4_basic.c src/math/vec3.c $(OBJ_GLAD) | build
	$(CC) $(CFLAGS) -D_POSIX_C_SOURCE=200809L $(RENDERER_TEST_CFLAGS) tests/visual/hall_bake_render.c src/mesh/dmesh_load.c src/mesh/obj_mesh_load.c src/lightmap/lm_lightmap_file.c src/lightmap/lm_material.c src/lightmap/lm_mesh_bake.c src/lightmap/lm_svo_voxelize.c src/lightmap/lm_gi_gather.c src/lightmap/lm_parallel.c src/lightmap/lm_mesh_luxel.c src/lightmap/lm_image.c src/lightmap/lm_farfield.c src/lightmap/lm_sky.c src/lightmap/lm_svo_mip.c src/lightmap/lm_svo_material.c src/lightmap/lm_kdtree.c src/lightmap/lm_solve.c src/lightmap/lm_visibility.c src/lightmap/lm_light.c src/lightmap/lm_atlas.c src/lightmap/lm_lightmap.c src/lightmap/lm_sh.c src/memory/arena_init.c src/memory/arena_alloc.c src/memory/arena_mark.c src/memory/arena_pop.c src/npc/nav/npc_svo_init.c src/npc/nav/npc_svo_rasterize.c src/npc/nav/npc_svo_blocker.c src/renderer/forward_plus.c src/renderer/cluster_grid.c src/renderer/render_camera.c src/renderer/pbr_shader.c src/renderer/material.c src/renderer/texture_create.c src/renderer/texture_upload.c src/renderer/texture_bind.c src/renderer/shader_program_create.c src/renderer/shader_program_bind.c src/renderer/shader_program_destroy.c src/renderer/shader_program_handle.c src/renderer/shader_uniforms_init.c src/renderer/gl_loader_validate.c src/math/mat4_proj.c src/math/mat4_look_at.c src/math/mat4_mul.c src/math/mat4_basic.c src/math/vec3.c $(OBJ_GLAD) -o $@ $(RENDERER_TEST_LIBS) -ldl -lm

# Headless (no-GL) hall bake -- runs the CPU voxel-GI bake + serializes the .flm.
LM_HALL_BAKE_SRC := src/mesh/dmesh_load.c src/mesh/obj_mesh_load.c \
  src/lightmap/lm_lightmap_file.c src/lightmap/lm_material.c src/lightmap/lm_mesh_bake.c \
  src/lightmap/lm_svo_voxelize.c src/lightmap/lm_gi_gather.c src/lightmap/lm_parallel.c \
  src/lightmap/lm_mesh_luxel.c src/lightmap/lm_image.c src/lightmap/lm_sky.c \
  src/lightmap/lm_svo_mip.c src/lightmap/lm_svo_material.c src/lightmap/lm_visibility.c \
  src/lightmap/lm_light.c src/lightmap/lm_atlas.c src/lightmap/lm_lightmap.c src/lightmap/lm_sh.c \
  src/memory/arena_init.c src/memory/arena_alloc.c src/memory/arena_mark.c src/memory/arena_pop.c \
  src/npc/nav/npc_svo_init.c src/npc/nav/npc_svo_rasterize.c src/npc/nav/npc_svo_blocker.c \
  src/math/vec3.c
build/hall_bake: tests/lightmap/hall_bake.c $(LM_HALL_BAKE_SRC) | build
	$(CC) $(CFLAGS) -D_POSIX_C_SOURCE=200809L tests/lightmap/hall_bake.c $(LM_HALL_BAKE_SRC) -o $@ -lm

# --- rpg-xkdz: hall through the clustered forward+ pipeline driver ---
build/hall_forward_pipeline: tests/visual/hall_forward_pipeline.c src/renderer/render_forward.c src/renderer/shadow_cube.c src/renderer/shadow_spot.c src/renderer/shadow_csm_init.c src/renderer/shadow_csm_cascade.c src/renderer/shadow_csm_render.c src/renderer/shadow_csm_bind.c src/renderer/resource/shadow_atlas.c src/renderer/resource/shadow_slotmap.c src/renderer/resource/gpu_registry_create.c src/renderer/resource/gpu_registry_access.c src/memory/pool_alloc.c src/memory/pool_destroy.c src/memory/pool_free.c src/memory/pool_get.c src/memory/pool_init.c src/renderer/depth_prepass.c src/renderer/render_pipeline_graph.c src/renderer/forward_plus.c src/renderer/cluster_grid.c src/renderer/render_camera.c src/renderer/render_scene.c src/renderer/light_store.c src/mesh/obj_mesh_load.c src/renderer/mesh/static_mesh_create.c src/renderer/mesh/static_mesh_destroy.c src/renderer/mesh/static_mesh_draw.c src/renderer/vao_create.c src/renderer/vao_bind_attributes.c src/renderer/vao_destroy.c src/renderer/vbo_create.c src/renderer/vbo_upload.c src/renderer/vbo_destroy.c src/renderer/pbr_shader.c src/renderer/material.c src/renderer/texture_create.c src/renderer/texture_upload.c src/renderer/texture_bind.c src/renderer/shader_program_create.c src/renderer/shader_program_bind.c src/renderer/shader_program_destroy.c src/renderer/shader_program_handle.c src/renderer/shader_uniforms_init.c src/renderer/gl_loader_validate.c src/math/mat4_proj.c src/math/mat4_look_at.c src/math/mat4_mul.c src/math/mat4_inverse.c src/math/mat4_basic.c src/math/vec3.c $(OBJ_GLAD) | build
	$(CC) $(CFLAGS) -D_POSIX_C_SOURCE=200809L $(RENDERER_TEST_CFLAGS) tests/visual/hall_forward_pipeline.c src/renderer/render_forward.c src/renderer/shadow_cube.c src/renderer/shadow_spot.c src/renderer/shadow_csm_init.c src/renderer/shadow_csm_cascade.c src/renderer/shadow_csm_render.c src/renderer/shadow_csm_bind.c src/renderer/resource/shadow_atlas.c src/renderer/resource/shadow_slotmap.c src/renderer/resource/gpu_registry_create.c src/renderer/resource/gpu_registry_access.c src/memory/pool_alloc.c src/memory/pool_destroy.c src/memory/pool_free.c src/memory/pool_get.c src/memory/pool_init.c src/renderer/depth_prepass.c src/renderer/render_pipeline_graph.c src/renderer/forward_plus.c src/renderer/cluster_grid.c src/renderer/render_camera.c src/renderer/render_scene.c src/renderer/light_store.c src/mesh/obj_mesh_load.c src/renderer/mesh/static_mesh_create.c src/renderer/mesh/static_mesh_destroy.c src/renderer/mesh/static_mesh_draw.c src/renderer/vao_create.c src/renderer/vao_bind_attributes.c src/renderer/vao_destroy.c src/renderer/vbo_create.c src/renderer/vbo_upload.c src/renderer/vbo_destroy.c src/renderer/pbr_shader.c src/renderer/material.c src/renderer/texture_create.c src/renderer/texture_upload.c src/renderer/texture_bind.c src/renderer/shader_program_create.c src/renderer/shader_program_bind.c src/renderer/shader_program_destroy.c src/renderer/shader_program_handle.c src/renderer/shader_uniforms_init.c src/renderer/gl_loader_validate.c src/math/mat4_proj.c src/math/mat4_look_at.c src/math/mat4_mul.c src/math/mat4_inverse.c src/math/mat4_basic.c src/math/vec3.c $(OBJ_GLAD) -o $@ $(RENDERER_TEST_LIBS) -ldl -lm

# --- rpg-xkdz: hall lightmapped + dynamic (SH lightmap through forward+) ---
build/hall_lit_dynamic: tests/visual/hall_lit_dynamic.c src/renderer/render_forward.c src/renderer/shadow_cube.c src/renderer/shadow_spot.c src/renderer/shadow_csm_init.c src/renderer/shadow_csm_cascade.c src/renderer/shadow_csm_render.c src/renderer/shadow_csm_bind.c src/renderer/resource/shadow_atlas.c src/renderer/resource/shadow_slotmap.c src/renderer/resource/gpu_registry_create.c src/renderer/resource/gpu_registry_access.c src/memory/pool_alloc.c src/memory/pool_destroy.c src/memory/pool_free.c src/memory/pool_get.c src/memory/pool_init.c src/memory/arena_alloc.c src/memory/arena_init.c src/memory/arena_mark.c src/memory/arena_pop.c src/memory/arena_reset.c src/memory/apool_alloc.c src/memory/apool_init.c src/memory/apool_get.c src/memory/apool_free.c src/memory/apool_destroy.c src/renderer/resource/gpu_cmd_queue.c src/renderer/resource/gpu_executor.c $(JOB_SRC) src/renderer/depth_prepass.c src/renderer/render_pipeline_graph.c src/renderer/forward_plus.c src/renderer/cluster_grid.c src/renderer/render_camera.c src/renderer/render_scene.c src/renderer/light_store.c src/mesh/obj_mesh_load.c src/mesh/dmesh_load.c src/renderer/mesh/static_mesh_create.c src/renderer/mesh/static_mesh_destroy.c src/renderer/mesh/static_mesh_draw.c src/renderer/mesh/static_mesh_primitives.c src/renderer/vao_create.c src/renderer/vao_bind_attributes.c src/renderer/vao_destroy.c src/renderer/vbo_create.c src/renderer/vbo_upload.c src/renderer/vbo_destroy.c src/renderer/pbr_shader.c src/renderer/material.c src/renderer/texture_create.c src/renderer/texture_upload.c src/renderer/texture_bind.c src/renderer/shader_program_create.c src/renderer/shader_program_bind.c src/renderer/shader_program_destroy.c src/renderer/shader_program_handle.c src/renderer/shader_uniforms_init.c src/renderer/gl_loader_validate.c src/math/mat4_proj.c src/math/mat4_look_at.c src/math/mat4_mul.c src/math/mat4_inverse.c src/math/mat4_basic.c src/math/vec3.c $(OBJ_GLAD) | build
	$(CC) $(CFLAGS) -D_POSIX_C_SOURCE=200809L $(RENDERER_TEST_CFLAGS) tests/visual/hall_lit_dynamic.c src/renderer/render_forward.c src/renderer/shadow_cube.c src/renderer/shadow_spot.c src/renderer/shadow_csm_init.c src/renderer/shadow_csm_cascade.c src/renderer/shadow_csm_render.c src/renderer/shadow_csm_bind.c src/renderer/resource/shadow_atlas.c src/renderer/resource/shadow_slotmap.c src/renderer/resource/gpu_registry_create.c src/renderer/resource/gpu_registry_access.c src/memory/pool_alloc.c src/memory/pool_destroy.c src/memory/pool_free.c src/memory/pool_get.c src/memory/pool_init.c src/memory/arena_alloc.c src/memory/arena_init.c src/memory/arena_mark.c src/memory/arena_pop.c src/memory/arena_reset.c src/memory/apool_alloc.c src/memory/apool_init.c src/memory/apool_get.c src/memory/apool_free.c src/memory/apool_destroy.c src/renderer/resource/gpu_cmd_queue.c src/renderer/resource/gpu_executor.c $(JOB_SRC) src/renderer/depth_prepass.c src/renderer/render_pipeline_graph.c src/renderer/forward_plus.c src/renderer/cluster_grid.c src/renderer/render_camera.c src/renderer/render_scene.c src/renderer/light_store.c src/mesh/obj_mesh_load.c src/mesh/dmesh_load.c src/renderer/mesh/static_mesh_create.c src/renderer/mesh/static_mesh_destroy.c src/renderer/mesh/static_mesh_draw.c src/renderer/mesh/static_mesh_primitives.c src/renderer/vao_create.c src/renderer/vao_bind_attributes.c src/renderer/vao_destroy.c src/renderer/vbo_create.c src/renderer/vbo_upload.c src/renderer/vbo_destroy.c src/renderer/pbr_shader.c src/renderer/material.c src/renderer/texture_create.c src/renderer/texture_upload.c src/renderer/texture_bind.c src/renderer/shader_program_create.c src/renderer/shader_program_bind.c src/renderer/shader_program_destroy.c src/renderer/shader_program_handle.c src/renderer/shader_uniforms_init.c src/renderer/gl_loader_validate.c src/math/mat4_proj.c src/math/mat4_look_at.c src/math/mat4_mul.c src/math/mat4_inverse.c src/math/mat4_basic.c src/math/vec3.c $(OBJ_GLAD) -o $@ $(RENDERER_TEST_LIBS) -ldl -lm
