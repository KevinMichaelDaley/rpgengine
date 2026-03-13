/**
 * @file clay_fonts.h
 * @brief Font loading and glyph atlas for Clay text rendering.
 *
 * Loads a TTF font into a GPU glyph atlas and provides the
 * Clay_SetMeasureTextFunction() callback for layout calculation.
 *
 * Ownership: clay_font_set_init() loads font data;
 *            clay_font_set_destroy() frees it.
 * Nullability: all pointer params must be non-NULL.
 * Error semantics: init returns false if font loading fails.
 * Side effects: creates a GL texture for the glyph atlas.
 *
 * Public types: clay_font_set_t, clay_font_id_t (2-type rule).
 */
#ifndef FERRUM_EDITOR_UI_CLAY_FONTS_H
#define FERRUM_EDITOR_UI_CLAY_FONTS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "ferrum/renderer/gl_loader.h"

/**
 * @brief Font identifier for multiple loaded fonts.
 */
typedef enum clay_font_id {
    CLAY_FONT_UI    = 0,  /**< UI labels (Inter, sans-serif). */
    CLAY_FONT_MONO  = 1,  /**< TUI / code (Fira Mono, monospace). */
    CLAY_FONT_COUNT = 2
} clay_font_id_t;

/**
 * @brief Glyph metrics for one character in the atlas.
 */
typedef struct clay_glyph {
    float advance_x;  /**< Horizontal advance in pixels. */
    float width;       /**< Glyph bitmap width. */
    float height;      /**< Glyph bitmap height. */
    float bearing_x;   /**< Left-side bearing. */
    float bearing_y;   /**< Top-side bearing. */
    float uv_x;        /**< Atlas U coordinate (left). */
    float uv_y;        /**< Atlas V coordinate (top). */
    float uv_w;        /**< Atlas U width. */
    float uv_h;        /**< Atlas V height. */
} clay_glyph_t;

/**
 * @brief Font set managing loaded fonts and their glyph atlases.
 */
typedef struct clay_font_set {
    uint32_t      atlas_texture;              /**< GL texture ID. */
    int           atlas_w;                    /**< Atlas width. */
    int           atlas_h;                    /**< Atlas height. */
    clay_glyph_t  glyphs[CLAY_FONT_COUNT][128]; /**< ASCII glyph metrics. */
    float         font_sizes[CLAY_FONT_COUNT]; /**< Font sizes in pixels. */
    bool          initialized;
    /** GL function for texture cleanup (resolved at init). */
    void (*glDeleteTextures)(int32_t n, const uint32_t *textures);
} clay_font_set_t;

/* ---- Lifecycle ---- */

/**
 * @brief Initialize the font set and generate glyph atlases.
 *
 * If TTF files are unavailable, falls back to a built-in bitmap font.
 * Requires a current OpenGL context.
 *
 * @param fonts  Font set to initialize (non-NULL).
 * @param loader GL function loader (non-NULL for GL atlas; NULL for headless).
 * @return true on success.
 */
bool clay_font_set_init(clay_font_set_t *fonts, const gl_loader_t *loader);

/**
 * @brief Destroy font set and free GL resources.
 * @param fonts  Font set (non-NULL).
 */
void clay_font_set_destroy(clay_font_set_t *fonts);

/**
 * @brief Get the advance width of a character.
 * @param fonts  Font set (non-NULL).
 * @param font   Font ID.
 * @param ch     ASCII character.
 * @return Advance width in pixels, or 0 for invalid characters.
 */
float clay_font_char_width(const clay_font_set_t *fonts, clay_font_id_t font,
                            char ch);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_UI_CLAY_FONTS_H */
