---
id: rpg-gf6y
status: closed
deps: []
links: []
created: 2026-02-13T19:18:40Z
type: task
priority: 2
assignee: KMD
---
# GPU-buffered video capture module

## Summary

Implement an efficient video capture module as a core renderer component (not demo-specific). The module captures rendered frames to video files without stalling the render loop.

## Architecture

### GPU-side buffering
- Maintain a ring of N PBOs (pixel buffer objects) for async GPU→CPU readback
- After each frame's render pass, blit or copy the default framebuffer (or a designated render target) into the next PBO
- Use GL fence sync objects to track when each PBO transfer completes
- Never glFinish or glReadPixels synchronously on the render thread

### CPU-side encoding thread
- Dedicated pthread polls completed PBO fences
- Maps completed PBOs, memcpys pixel data to a CPU-side ring buffer, unmaps
- Encodes frames to a container format (raw RGBA → PPM as MVP, or pipe to ffmpeg for H.264/MP4)
- Writes encoded data to file
- Thread communicates with render thread only via the PBO ring (lock-free)

### Public API (sketch)
```c
typedef struct fr_video_capture fr_video_capture_t;

// Create capture context. width/height = render target dimensions.
fr_video_capture_t *fr_video_capture_create(int width, int height, const char *output_path);

// Called each frame from render thread after drawing.
// Submits async PBO readback, returns immediately.
void fr_video_capture_submit_frame(fr_video_capture_t *ctx);

// Stop capture, flush remaining frames, close file.
void fr_video_capture_destroy(fr_video_capture_t *ctx);
```

### Module location
- Header: `include/ferrum/renderer/video_capture.h`
- Sources: `src/renderer/video_capture/` (pbo_ring.c, encode_thread.c, video_capture.c)

### Key constraints
- Zero-copy on GPU side (PBO DMA readback)
- Render thread never blocks waiting for encode
- Encode thread never touches GL context (only mapped PBO memory)
- Must handle backpressure: if encoder falls behind, drop oldest unread frames
- Frame timestamps for correct playback rate
- No third-party codec libraries (pipe to ffmpeg or write raw frames)

