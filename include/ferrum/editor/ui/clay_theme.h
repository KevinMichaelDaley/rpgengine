/**
 * @file clay_theme.h
 * @brief Color palette, spacing, and font size constants for the editor UI.
 *
 * All theme values are compile-time constants. No dynamic allocation.
 *
 * Public types: none (constants only).
 */
#ifndef FERRUM_EDITOR_UI_CLAY_THEME_H
#define FERRUM_EDITOR_UI_CLAY_THEME_H

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Background colors (RGBA 0-255) ---- */

#define THEME_BG_PANEL_R      30
#define THEME_BG_PANEL_G      32
#define THEME_BG_PANEL_B      36
#define THEME_BG_PANEL_A      255

#define THEME_BG_VIEWPORT_R   20
#define THEME_BG_VIEWPORT_G   22
#define THEME_BG_VIEWPORT_B   26
#define THEME_BG_VIEWPORT_A   255

#define THEME_BG_TUI_R        18
#define THEME_BG_TUI_G        18
#define THEME_BG_TUI_B        22
#define THEME_BG_TUI_A        255

/* ---- Accent colors ---- */

#define THEME_ACCENT_R        80
#define THEME_ACCENT_G        140
#define THEME_ACCENT_B        220
#define THEME_ACCENT_A        255

#define THEME_SELECTION_R     230
#define THEME_SELECTION_G     150
#define THEME_SELECTION_B     30
#define THEME_SELECTION_A     255

/* ---- Text colors ---- */

#define THEME_TEXT_R           220
#define THEME_TEXT_G           220
#define THEME_TEXT_B           220
#define THEME_TEXT_A           255

#define THEME_TEXT_DIM_R       140
#define THEME_TEXT_DIM_G       140
#define THEME_TEXT_DIM_B       140
#define THEME_TEXT_DIM_A       255

/* ---- Divider ---- */

#define THEME_DIVIDER_R       60
#define THEME_DIVIDER_G       62
#define THEME_DIVIDER_B       66
#define THEME_DIVIDER_A       255
#define THEME_DIVIDER_WIDTH   4

/* ---- Spacing (in pixels) ---- */

#define THEME_PADDING         8
#define THEME_PADDING_SMALL   4
#define THEME_MARGIN          4
#define THEME_ROW_HEIGHT      24
#define THEME_INDENT          16

/* ---- Font sizes ---- */

#define THEME_FONT_SIZE_UI    14
#define THEME_FONT_SIZE_MONO  13
#define THEME_FONT_SIZE_TITLE 18

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_UI_CLAY_THEME_H */
