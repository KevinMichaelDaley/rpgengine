/**
 * @file procgen_layout.h
 * @brief Dungeon layout structs — the intermediate representation
 *        produced by the rasterizer and consumed by the serializer,
 *        critic, and engine entity spawner.
 */

#ifndef FERRUM_PROCGEN_LAYOUT_H
#define FERRUM_PROCGEN_LAYOUT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "ferrum/math/vec3.h"
#include "ferrum/procgen/procgen_types.h"

/* ── Room ─────────────────────────────────────────────────────── */

/**
 * @brief A room in the dungeon layout.
 *
 * A room is a convex polytope defined by a polygon in the XY plane
 * extruded vertically from floor_z to ceil_z.
 */
typedef struct {
    uint32_t vertex_count;             /**< Number of vertices (4-8). */
    vec3_t   vertices[8];              /**< Polygon vertices (XY, Z=floor_z). */
    float    floor_z;                  /**< Floor height (world Z). */
    float    ceil_z;                   /**< Ceiling height (world Z). */
    char     name[64];                 /**< Optional room name. */
} fr_room_def_t;

/* ── Corridor ─────────────────────────────────────────────────── */

/**
 * @brief A corridor connecting two rooms.
 *
 * Defined as a line segment (from → to) extruded laterally by width/2
 * in each direction, then vertically from floor_z to ceil_z.
 */
typedef struct {
    vec3_t              from;         /**< Start point (world space). */
    vec3_t              to;           /**< End point (world space). */
    float               width;        /**< Lateral width of the corridor. */
    float               floor_z;      /**< Floor height. */
    float               ceil_z;       /**< Ceiling height. */
    fr_corridor_angle_t angle_type;   /**< Allowed angle classification. */
} fr_corridor_def_t;

/* ── Opening (door / window) ──────────────────────────────────── */

/**
 * @brief A door or window opening in a wall.
 */
typedef struct {
    vec3_t            pos;            /**< World-space position of the opening. */
    float             width;          /**< Opening width. */
    float             height;         /**< Opening height. */
    fr_opening_type_t type;           /**< Door or window. */
} fr_opening_def_t;

/* ── Ramp ─────────────────────────────────────────────────────── */

/**
 * @brief A sloped ramp connecting two floor heights.
 */
typedef struct {
    vec3_t from;            /**< Start of the ramp (world space). */
    vec3_t to;              /**< End of the ramp (world space). */
    float  height_change;   /**< Vertical change (positive = up). */
    float  width;           /**< Lateral width of the ramp. */
} fr_ramp_def_t;

/* ── Marker ───────────────────────────────────────────────────── */

/**
 * @brief A named waypoint in the dungeon.
 *
 * Markers are designated by the architect VLM and must be reachable
 * by the player. The critic validates marker reachability.
 */
typedef struct {
    vec3_t pos;                 /**< World-space position. */
    char   name[64];            /**< Marker name (e.g., "boss_arena"). */
} fr_marker_def_t;

/* ── Navigation graph ─────────────────────────────────────────── */

/**
 * @brief A node in the dungeon navigation graph.
 */
typedef struct {
    fr_nav_node_type_t type;       /**< Room node or junction node. */
    vec3_t             pos;        /**< World-space position. */
    uint32_t           room_index; /**< Index into fr_dungeon_layout_t.rooms (if NAV_ROOM). */
} fr_nav_node_t;

/**
 * @brief An edge in the dungeon navigation graph.
 *
 * Represents a traversable connection between two nav nodes.
 */
typedef struct {
    uint32_t from_node;            /**< Source node index. */
    uint32_t to_node;              /**< Destination node index. */
    float    distance;             /**< Travel distance (world units). */
} fr_nav_edge_t;

/* ── Complete dungeon layout ──────────────────────────────────── */

/**
 * @brief The complete intermediate representation of a dungeon level.
 *
 * Produced by the rasterizer from a token stream. Contains all
 * geometry, spawn point, navigation graph, and generation metadata.
 * Arrays are owned by the layout and must be freed by the caller.
 */
typedef struct {
    /* ── Version ── */
    uint32_t version;                /**< Layout format version. */

    /* ── Geometry ── */
    uint32_t          room_count;     /**< Number of rooms. */
    fr_room_def_t    *rooms;          /**< Room array (or NULL). */
    uint32_t          corridor_count; /**< Number of corridors. */
    fr_corridor_def_t *corridors;     /**< Corridor array (or NULL). */
    uint32_t          opening_count;  /**< Number of openings. */
    fr_opening_def_t *openings;       /**< Opening array (or NULL). */
    uint32_t          ramp_count;     /**< Number of ramps. */
    fr_ramp_def_t    *ramps;          /**< Ramp array (or NULL). */
    uint32_t          marker_count;   /**< Number of markers. */
    fr_marker_def_t  *markers;        /**< Marker array (or NULL). */

    /* ── Spawn ── */
    vec3_t spawn_pos;                /**< Player spawn position. */

    /* ── Navigation graph ── */
    uint32_t     nav_node_count;     /**< Number of nav nodes. */
    fr_nav_node_t *nav_nodes;        /**< Nav node array (or NULL). */
    uint32_t     nav_edge_count;     /**< Number of nav edges. */
    fr_nav_edge_t *nav_edges;        /**< Nav edge array (or NULL). */

    /* ── Metadata ── */
    char     grammar_name[64];       /**< Name of the grammar used. */
    uint32_t grammar_version;        /**< Version of the grammar used. */
    char     raw_token_string[8192]; /**< Original token string (null-terminated). */
} fr_dungeon_layout_t;

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_PROCGEN_LAYOUT_H */
