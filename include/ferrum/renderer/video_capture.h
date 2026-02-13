/**
 * @file video_capture.h
 * @brief GPU-buffered video capture with async PBO readback.
 *
 * Captures rendered frames to a video file without stalling the render
 * loop.  A ring of PBOs handles async GPU→CPU transfer on the render
 * thread; a dedicated encoder pthread consumes completed frames from a
 * CPU-side ring buffer and pipes them to ffmpeg (or writes raw RGBA).
 *
 * ## Usage
 * @code
 *   fr_video_capture_t *cap = fr_video_capture_create(&(fr_video_capture_desc_t){
 *       .width  = 1280,
 *       .height = 720,
 *       .fps    = 60,
 *       .output_path = "capture.mp4",
 *   });
 *
 *   // Each frame, after drawing and before SwapWindow:
 *   fr_video_capture_submit_frame(cap);
 *
 *   // When done:
 *   fr_video_capture_destroy(cap);
 * @endcode
 *
 * ## Ownership
 * - Caller owns the returned pointer; must call destroy to free.
 * - The module borrows the current GL context (must be current on the
 *   calling thread for create/submit/destroy).
 *
 * ## Thread safety
 * - create, submit_frame, destroy must all be called from the render
 *   thread (the thread with the GL context).
 * - Internally spawns one encoder thread that never touches GL.
 */

#ifndef FR_VIDEO_CAPTURE_H
#define FR_VIDEO_CAPTURE_H

#include <stdint.h>

/** Opaque video capture context. */
typedef struct fr_video_capture fr_video_capture_t;

/** Configuration for creating a video capture context. */
typedef struct fr_video_capture_desc {
    int         width;        /**< Framebuffer width in pixels. */
    int         height;       /**< Framebuffer height in pixels. */
    int         fps;          /**< Target frames per second for output. */
    const char *output_path;  /**< Output file path (e.g. "out.mp4"). */
} fr_video_capture_desc_t;

/**
 * @brief Create a video capture context.
 *
 * Allocates PBO ring, CPU frame ring, and spawns the encoder thread.
 * The GL context must be current on the calling thread.
 *
 * @param desc  Capture configuration.  Must not be NULL.
 * @return Capture context, or NULL on failure.
 *
 * @note Ownership: caller must call fr_video_capture_destroy().
 * @note Side effects: allocates GPU buffers, spawns a thread, may
 *       open a pipe to ffmpeg or a file handle.
 */
fr_video_capture_t *fr_video_capture_create(const fr_video_capture_desc_t *desc);

/**
 * @brief Submit the current framebuffer for capture.
 *
 * Call once per frame on the render thread, after drawing and before
 * SDL_GL_SwapWindow.  This function:
 *   1. Harvests any PBOs whose fence has signalled (GPU→CPU complete),
 *      maps them, copies pixel data into the CPU frame ring, unmaps.
 *   2. Initiates a new async PBO readback of the default framebuffer.
 *
 * If the CPU frame ring is full (encoder behind), the oldest unread
 * frame is silently dropped.
 *
 * @param cap  Capture context.  Must not be NULL.
 *
 * @note Side effects: GL commands (glReadPixels into PBO, fence).
 *       Never blocks on GPU.
 */
void fr_video_capture_submit_frame(fr_video_capture_t *cap);

/**
 * @brief Stop capture, flush remaining frames, and free resources.
 *
 * Signals the encoder thread to drain and exit, joins it, deletes
 * PBOs and fences, frees all memory.  The GL context must be current.
 *
 * @param cap  Capture context (NULL is a safe no-op).
 *
 * @note Side effects: joins encoder thread, closes file/pipe,
 *       deletes GL objects, frees memory.
 */
void fr_video_capture_destroy(fr_video_capture_t *cap);

/**
 * @brief Query how many frames have been written so far.
 *
 * @param cap  Capture context.  Must not be NULL.
 * @return Number of frames successfully written to output.
 */
uint64_t fr_video_capture_frames_written(const fr_video_capture_t *cap);

#endif /* FR_VIDEO_CAPTURE_H */
