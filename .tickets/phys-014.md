---
id: phys-014
status: open
deps: [phys-001, phys-002]
links: [phys-000]
created: 2026-02-06T06:56:00.000000000-08:00
type: task
priority: 1
---
# Step 0.14: Game State Input Structure

**Parent Epic:** phys-000 (Phase 0: Foundation Data Structures)

## Description

Define the input structure that provides gameplay context to the physics
system. This includes player positions (for tier classification), camera
position (for LOD), and manipulation flags (for T0 promotion).

The physics system reads this but does not own it—it's provided by gameplay
each tick.

## Files to create

- `include/ferrum/physics/game_state.h`
- `tests/physics/game_state_tests.c`

## Structures

```c
#define PHYS_MAX_PLAYERS 16

typedef struct phys_manipulation_t {
    pool_handle_t body;       // body being manipulated
    uint8_t manipulation_type; // grab, push, carry, etc.
} phys_manipulation_t;

typedef struct phys_player_state_t {
    phys_vec3_t position;
    phys_vec3_t velocity;
    phys_vec3_t look_direction;
    float interaction_radius;  // how far player can reach
    phys_manipulation_t manipulation; // currently manipulated body (if any)
    bool has_manipulation;
} phys_player_state_t;

typedef struct phys_game_state_t {
    // Player data
    phys_player_state_t players[PHYS_MAX_PLAYERS];
    uint32_t player_count;
    
    // Camera (for LOD decisions)
    phys_vec3_t camera_position;
    phys_vec3_t camera_forward;
    float camera_fov_rad;
    
    // Gameplay hints
    uint32_t *hazard_body_indices;  // bodies marked as hazardous
    uint32_t hazard_count;
    
    // Time
    float game_time;
    float time_scale;  // for slow-mo effects
} phys_game_state_t;
```

## API

```c
void phys_game_state_init(phys_game_state_t *state);
void phys_game_state_set_player(phys_game_state_t *state, uint32_t index,
                                 const phys_player_state_t *player);
void phys_game_state_set_camera(phys_game_state_t *state,
                                 phys_vec3_t position, phys_vec3_t forward, float fov);
void phys_game_state_add_hazard(phys_game_state_t *state, pool_handle_t body);
void phys_game_state_clear_hazards(phys_game_state_t *state);

// Query helpers for physics stages
bool phys_game_state_is_manipulated(const phys_game_state_t *state, pool_handle_t body);
float phys_game_state_distance_to_nearest_player(const phys_game_state_t *state, phys_vec3_t pos);
bool phys_game_state_is_in_view(const phys_game_state_t *state, phys_vec3_t pos, float radius);
```

## Usage

```c
// Gameplay fills this each frame before physics tick
phys_game_state_t game;
phys_game_state_init(&game);
phys_game_state_set_player(&game, 0, &player_state);
phys_game_state_set_camera(&game, cam_pos, cam_fwd, fov);

// Physics uses it
phys_world_tick_with_state(&world, &game);
// or
phys_stage_step_plan(&plan, &world, &game);
phys_stage_tier_classify(&args);  // args includes game state
```

## Acceptance Criteria

- [ ] Structure defined with all fields needed by tier classification
- [ ] Player state includes position, velocity, manipulation
- [ ] Camera state enables view frustum-based LOD
- [ ] Query helpers are O(player_count) or better
- [ ] Works with NULL game state (use defaults)

## Test Cases

```c
// test_game_state_init
phys_game_state_t state;
phys_game_state_init(&state);
ASSERT(state.player_count == 0);
ASSERT(state.hazard_count == 0);

// test_game_state_set_player
phys_player_state_t player = {
    .position = {10, 0, 10},
    .velocity = {1, 0, 0},
    .interaction_radius = 3.0f,
    .has_manipulation = false
};
phys_game_state_set_player(&state, 0, &player);
ASSERT(state.player_count == 1);
ASSERT_VEC3_EQ(state.players[0].position, player.position);

// test_distance_to_nearest_player
phys_game_state_set_player(&state, 1, &(phys_player_state_t){
    .position = {20, 0, 10}
});
float dist = phys_game_state_distance_to_nearest_player(&state, (phys_vec3_t){12, 0, 10});
ASSERT_FLOAT_NEAR(dist, 2.0f, 0.01f);  // closest to player 0

// test_is_manipulated
state.players[0].has_manipulation = true;
state.players[0].manipulation.body = (pool_handle_t){.index = 42, .generation = 1};
ASSERT(phys_game_state_is_manipulated(&state, (pool_handle_t){.index = 42, .generation = 1}));
ASSERT(!phys_game_state_is_manipulated(&state, (pool_handle_t){.index = 43, .generation = 1}));

// test_is_in_view
phys_game_state_set_camera(&state, (phys_vec3_t){0,0,0}, (phys_vec3_t){0,0,1}, M_PI/3);
ASSERT(phys_game_state_is_in_view(&state, (phys_vec3_t){0, 0, 10}, 1.0f));  // in front
ASSERT(!phys_game_state_is_in_view(&state, (phys_vec3_t){0, 0, -10}, 1.0f)); // behind
```
