/**
 * @file scene_input.h
 * @brief SDL2 event dispatch for the scene editor.
 *
 * Translates SDL2 events into panel-specific actions: focus changes,
 * divider drags, and key/mouse dispatch to the focused panel.
 *
 * Ownership: no allocations; operates on scene_editor_t in place.
 * Nullability: all pointer params must be non-NULL.
 * Error semantics: unknown events are silently ignored.
 * Side effects: modifies scene editor state (focus, layout, dividers).
 *
 * Public types: none (uses scene_editor_t and SDL types).
 */
#ifndef FERRUM_EDITOR_SCENE_INPUT_H
#define FERRUM_EDITOR_SCENE_INPUT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

/* Forward declarations */
struct scene_editor;
union SDL_Event;

/**
 * @brief Process a single SDL2 event.
 *
 * Handles window resize, quit, mouse clicks (focus, divider drag start),
 * mouse motion (divider drag), key events (panel toggles, focus cycling).
 *
 * @param ed    Scene editor context (non-NULL).
 * @param event SDL event to process (non-NULL).
 * @return true if the event was consumed (should not be passed further).
 */
bool scene_input_process(struct scene_editor *ed, const union SDL_Event *event);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_SCENE_INPUT_H */
