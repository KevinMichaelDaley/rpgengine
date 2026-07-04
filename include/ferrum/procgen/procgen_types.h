/**
 * @file procgen_types.h
 * @brief Core token and geometry types for the procedural dungeon grammar system.
 *
 * Defines the token type enum, token structure, error codes, and the
 * fundamental geometry primitives used by all grammars.
 */

#ifndef FERRUM_PROCGEN_TYPES_H
#define FERRUM_PROCGEN_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "ferrum/math/vec3.h"

/* ── Token type enum ─────────────────────────────────────────── */

/** @brief Grammar token types — the alphabet of the token string format. */
typedef enum {
    TOK_GRAMMAR       = 0,   /**< @grammar header (name + version). */
    TOK_ROOM_QUAD     = 1,   /**< Rectangular room (x,y,w,h,floor_z,ceil_z). */
    TOK_ROOM_PENT     = 2,   /**< 5-sided convex polygon room. */
    TOK_CORRIDOR_H    = 3,   /**< Horizontal corridor (y values equal). */
    TOK_CORRIDOR_V    = 4,   /**< Vertical corridor (x values equal). */
    TOK_CORRIDOR_DIAG = 5,   /**< Diagonal corridor (45° or 30°/60°). */
    TOK_RAMP_UP       = 6,   /**< Upward-sloping ramp (positive dz). */
    TOK_RAMP_DOWN     = 7,   /**< Downward-sloping ramp (negative dz). */
    TOK_DOOR          = 8,   /**< Door opening between rooms. */
    TOK_WINDOW        = 9,   /**< Window opening in a wall. */
    TOK_SPAWN         = 10,  /**< Player spawn point (x,y,z). */
    TOK_MARKER        = 11,  /**< Named waypoint marker (x,y,z,name). */
    TOK_BLOCK         = 12,  /**< Open scope / begin block. */
    TOK_EBLOCK        = 13,  /**< Close scope / end block. */
    TOK_CONNECT       = 14,  /**< Explicit connectivity hint. */
    TOK_JUNCTION      = 15,  /**< Junction / intersection point. */
    TOK_ERROR         = 16   /**< Sentinel: tokenizer error encountered. */
} tok_type_t;

/* ── Tokenizer error codes ───────────────────────────────────── */

/** @brief Error codes returned by the tokenizer. */
typedef enum {
    TOK_ERR_NONE              = 0,   /**< No error. */
    TOK_ERR_UNEXPECTED_TOKEN  = -1,  /**< Unexpected character or keyword. */
    TOK_ERR_UNBALANCED_BLOCK  = -2,  /**< Mismatched BLOCK/EBLOCK nesting. */
    TOK_ERR_MISSING_PARAM     = -3,  /**< Required parameter not provided. */
    TOK_ERR_INVALID_NUMBER    = -4,  /**< Malformed numeric literal. */
    TOK_ERR_BUFFER_FULL       = -5,  /**< Output token buffer exhausted. */
    TOK_ERR_INTERNAL          = -6   /**< Internal error (should not happen). */
} tok_error_t;

/* ── Token struct ─────────────────────────────────────────────── */

/**
 * @brief A single parsed token from the token string.
 *
 * Contains the token type, source location (for error reporting),
 * and any associated value (int, float, or string).
 */
typedef struct procgen_token {
    tok_type_t type;            /**< Token classification. */
    uint32_t   line;            /**< Source line number (1-based). */
    uint32_t   col;             /**< Source column number (1-based). */
    uint32_t   grammar_version; /**< Grammar version (valid for TOK_GRAMMAR only). */
    union {
        int32_t  i;             /**< Integer value (counts, indices). */
        float    f;             /**< Float value (coordinates, dimensions). */
        uint32_t u;             /**< Unsigned integer value. */
        char     s[64];         /**< String value (names, labels). */
    } value;                    /**< Token payload. */
} procgen_token_t;

/* ── 2D integer vector (grid coordinates) ─────────────────────── */

/** @brief 2D integer vector for grid-aligned coordinates. */
typedef struct {
    int32_t x;                  /**< X coordinate. */
    int32_t y;                  /**< Y coordinate. */
} fr_vec2i_t;

/* ── Corridor angle type ──────────────────────────────────────── */

/** @brief Allowed corridor angle classifications. */
typedef enum {
    CORR_ANGLE_H     = 0,  /**< Horizontal (y aligned, ±x axis). */
    CORR_ANGLE_V     = 1,  /**< Vertical (x aligned, ±y axis). */
    CORR_ANGLE_45    = 2,  /**< 45-degree diagonal. */
    CORR_ANGLE_30_60 = 3   /**< 30/60-degree diagonal. */
} fr_corridor_angle_t;

/* ── Opening type ─────────────────────────────────────────────── */

/** @brief Types of wall openings. */
typedef enum {
    OPEN_DOOR   = 0,  /**< Door — passable entry/exit. */
    OPEN_WINDOW = 1   /**< Window — non-passable aperture. */
} fr_opening_type_t;

/* ── Navigation node type ─────────────────────────────────────── */

/** @brief Types of navigation graph nodes. */
typedef enum {
    NAV_ROOM     = 0,  /**< Node represents a room. */
    NAV_JUNCTION = 1   /**< Node represents a corridor junction. */
} fr_nav_node_type_t;

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_PROCGEN_TYPES_H */
