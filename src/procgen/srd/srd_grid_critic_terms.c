/**
 * @file srd_grid_critic_terms.c
 * @brief Individual loss term computations for the grid-based critic.
 *
 * Non-static functions (4):
 *   srd_grid_critic_volume
 *   srd_grid_critic_reachability
 *   srd_grid_critic_bounds
 *   srd_grid_critic_separation
 */
#include "ferrum/procgen/srd/srd_grid_critic.h"
#include "ferrum/procgen/srd/srd_room_type.h"

#include <stdlib.h>
#include <string.h>

/* ── Volume term ──────────────────────────────────────────────────── */

/**
 * @brief Compute the volume penalty.
 *
 * For each room, count voxels. If count < min_room_voxels, add
 * (min - count) / min to the penalty. Returns the average penalty
 * across all rooms.
 */
float srd_grid_critic_volume(const srd_room_map_t *map,
                             int min_room_voxels) {
    if (!map || map->n_rooms <= 0 || min_room_voxels <= 0) return 0.0f;

    float penalty = 0.0f;
    for (int r = 1; r <= map->n_rooms; r++) {
        int vol = srd_room_map_count_volume(map, (uint8_t)r);
        if (vol < min_room_voxels) {
            penalty += (float)(min_room_voxels - vol) / (float)min_room_voxels;
        }
    }
    return penalty / (float)map->n_rooms;
}

/* ── Reachability term ────────────────────────────────────────────── */

/**
 * @brief Find the entrance room (first room with type SRD_ROOM_ENTRANCE).
 * @return 1-based room ID, or 0 if no entrance found.
 */
static uint8_t find_entrance(const srd_room_map_t *map) {
    for (int r = 0; r < map->n_rooms; r++) {
        if (map->types[r] == SRD_ROOM_ENTRANCE) return (uint8_t)(r + 1);
    }
    return 0;
}

/**
 * @brief Compute the reachability penalty.
 *
 * Flood-fills from the entrance room through negative-SDF voxels.
 * Any room not reached contributes 1/n_rooms to the penalty.
 */
float srd_grid_critic_reachability(const srd_sdf_grid_t *grid,
                                   const srd_room_map_t *map) {
    if (!grid || !grid->values || !map || !map->ids || map->n_rooms <= 0)
        return 0.0f;

    uint8_t entrance_id = find_entrance(map);
    if (entrance_id == 0) return 0.0f;

    int total = grid->nx * grid->ny * grid->nz;

    /* Visited bitmap */
    uint8_t *visited = calloc((size_t)total, 1);
    if (!visited) return 0.0f;

    /* Room reached flags (1-based indexing, so size n_rooms+1) */
    uint8_t *reached = calloc((size_t)(map->n_rooms + 1), 1);
    if (!reached) { free(visited); return 0.0f; }

    /* BFS queue — worst case every voxel */
    int *queue = malloc(sizeof(int) * (size_t)total);
    if (!queue) { free(visited); free(reached); return 0.0f; }

    int head = 0, tail = 0;

    /* Seed BFS with all voxels belonging to the entrance room that have SDF < 0 */
    for (int i = 0; i < total; i++) {
        if (map->ids[i] == entrance_id && grid->values[i] < 0.0f) {
            visited[i] = 1;
            queue[tail++] = i;
        }
    }
    reached[entrance_id] = 1;

    int nx = grid->nx, ny = grid->ny, nz = grid->nz;

    /* 6-connected BFS through negative-SDF voxels */
    while (head < tail) {
        int idx = queue[head++];
        int z = idx / (ny * nx);
        int rem = idx % (ny * nx);
        int y = rem / nx;
        int x = rem % nx;

        /* 6 neighbours */
        int dx[6] = {-1, 1,  0, 0,  0, 0};
        int dy[6] = { 0, 0, -1, 1,  0, 0};
        int dz[6] = { 0, 0,  0, 0, -1, 1};

        for (int d = 0; d < 6; d++) {
            int nx2 = x + dx[d];
            int ny2 = y + dy[d];
            int nz2 = z + dz[d];
            if (nx2 < 0 || nx2 >= nx || ny2 < 0 || ny2 >= ny ||
                nz2 < 0 || nz2 >= nz)
                continue;

            int ni = nz2 * ny * nx + ny2 * nx + nx2;
            if (visited[ni]) continue;
            if (grid->values[ni] >= 0.0f) continue; /* solid wall — blocked */

            visited[ni] = 1;
            queue[tail++] = ni;

            /* Mark room as reached */
            uint8_t rid = map->ids[ni];
            if (rid > 0 && rid <= map->n_rooms) {
                reached[rid] = 1;
            }
        }
    }

    /* Count unreachable rooms */
    int unreachable = 0;
    for (int r = 1; r <= map->n_rooms; r++) {
        if (!reached[r]) unreachable++;
    }

    free(queue);
    free(reached);
    free(visited);

    if (map->n_rooms <= 0) return 0.0f;
    return (float)unreachable / (float)map->n_rooms;
}

/* ── Bounds violation term ────────────────────────────────────────── */

/**
 * @brief Compute the bounds violation penalty.
 *
 * Checks the 6 boundary faces of the grid. Any voxel on a face with
 * SDF < 0 (room interior) is a violation. Returns the fraction of
 * boundary voxels that violate.
 */
float srd_grid_critic_bounds(const srd_sdf_grid_t *grid) {
    if (!grid || !grid->values) return 0.0f;

    int nx = grid->nx, ny = grid->ny, nz = grid->nz;
    int violations = 0;
    int boundary_count = 0;

    for (int z = 0; z < nz; z++) {
        for (int y = 0; y < ny; y++) {
            for (int x = 0; x < nx; x++) {
                /* Check if this voxel is on any face */
                if (x == 0 || x == nx - 1 ||
                    y == 0 || y == ny - 1 ||
                    z == 0 || z == nz - 1) {
                    boundary_count++;
                    int idx = z * ny * nx + y * nx + x;
                    if (grid->values[idx] < 0.0f) {
                        violations++;
                    }
                }
            }
        }
    }

    if (boundary_count <= 0) return 0.0f;
    return (float)violations / (float)boundary_count;
}

/* ── Type separation term ─────────────────────────────────────────── */

/**
 * @brief Check if a room-type adjacency is penalized.
 *
 * Bad pairings: BOSS adjacent to ENTRANCE, TREASURE adjacent to ENTRANCE.
 */
static int is_bad_adjacency(srd_room_type_t a, srd_room_type_t b) {
    /* Boss should not be adjacent to entrance */
    if ((a == SRD_ROOM_ENTRANCE && b == SRD_ROOM_BOSS) ||
        (a == SRD_ROOM_BOSS && b == SRD_ROOM_ENTRANCE))
        return 1;

    /* Treasure should not be adjacent to entrance */
    if ((a == SRD_ROOM_ENTRANCE && b == SRD_ROOM_TREASURE) ||
        (a == SRD_ROOM_TREASURE && b == SRD_ROOM_ENTRANCE))
        return 1;

    return 0;
}

/**
 * @brief Compute the type separation penalty.
 *
 * Scans the adjacency matrix for bad pairings. Returns the fraction
 * of room pairs that have bad type adjacency.
 */
float srd_grid_critic_separation(const srd_room_map_t *map) {
    if (!map || map->n_rooms <= 1) return 0.0f;

    int bad = 0;
    int total_pairs = 0;

    for (int a = 1; a <= map->n_rooms; a++) {
        for (int b = a + 1; b <= map->n_rooms; b++) {
            if (srd_room_map_are_adjacent(map, (uint8_t)a, (uint8_t)b)) {
                total_pairs++;
                srd_room_type_t ta = srd_room_map_get_type(map, (uint8_t)a);
                srd_room_type_t tb = srd_room_map_get_type(map, (uint8_t)b);
                if (is_bad_adjacency(ta, tb)) {
                    bad++;
                }
            }
        }
    }

    if (total_pairs <= 0) return 0.0f;
    return (float)bad / (float)total_pairs;
}
