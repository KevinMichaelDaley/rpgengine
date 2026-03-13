/**
 * @file scene_frame.h
 * @brief Scene editor per-frame update — server sync, action dispatch.
 *
 * Called each frame to pump the server connection, process responses,
 * and dispatch UI actions (spawn, select, delete, etc.) as server commands.
 *
 * Public types: none (0-type, under 2-type limit).
 */
#ifndef FERRUM_EDITOR_SCENE_FRAME_H
#define FERRUM_EDITOR_SCENE_FRAME_H

#ifdef __cplusplus
extern "C" {
#endif

struct scene_editor;

/**
 * @brief Pump server connection and process responses.
 *
 * Reads available TCP data, extracts complete response lines,
 * parses JSON, and updates the local entity store.
 *
 * @param ed  Editor context.
 */
void scene_frame_pump(struct scene_editor *ed);

/**
 * @brief Dispatch pending UI action as a server command.
 *
 * Checks ed->ui.action and sends the appropriate command
 * to the server, then clears the action.
 *
 * @param ed  Editor context.
 */
void scene_frame_dispatch_action(struct scene_editor *ed);

/**
 * @brief Request a full entity list from the server.
 *
 * Sends list_entities command. Response will be processed
 * by scene_frame_pump on a subsequent frame.
 *
 * @param ed  Editor context.
 */
void scene_frame_request_entity_list(struct scene_editor *ed);

/**
 * @brief Flush the offline command queue after reconnect.
 *
 * Replays all queued TUI commands that were entered while disconnected.
 *
 * @param ed  Editor context.
 */
void scene_frame_flush_offline_queue(struct scene_editor *ed);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_SCENE_FRAME_H */
