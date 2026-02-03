#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include "client_fiber_internal.h"

bool fr_server_client_fiber_inject_frame(fr_server_client_fiber_t *fiber, const unsigned char *frame, size_t len) {
    if (!fiber || !fiber->stream || !frame) return false;
    if (len < 4) return false; /* header too small */
    /* Delegate validation to stream (payload size, header parse). */
    return fr_rudp_stream_push_frame(fiber->stream, frame, len);
}
