#ifndef FERRUM_RENDERER_RENDER_RESOURCE_VIEWS_H
#define FERRUM_RENDERER_RENDER_RESOURCE_VIEWS_H

#include <stddef.h>
#include <stdint.h>

/** @file
 * @brief Render pipeline resource view declarations.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** Resource attachment type. */
typedef enum render_resource_attachment_type {
    RENDER_RESOURCE_ATTACHMENT_COLOR = 0,
    RENDER_RESOURCE_ATTACHMENT_DEPTH = 1
} render_resource_attachment_type_t;

/** Attachment format identifier. */
typedef enum render_resource_format {
    RENDER_RESOURCE_FORMAT_RGBA8 = 0,
    RENDER_RESOURCE_FORMAT_DEPTH24_STENCIL8 = 1
} render_resource_format_t;

/** Attachment resource description. */
typedef struct render_resource_attachment {
    render_resource_attachment_type_t type;
    render_resource_format_t format;
    uint32_t handle;
    uint32_t width;
    uint32_t height;
} render_resource_attachment_t;

/** Transient resource type. */
typedef enum render_resource_type {
    RENDER_RESOURCE_TYPE_BUFFER = 0
} render_resource_type_t;

/** Transient resource usage. */
typedef enum render_resource_usage {
    RENDER_RESOURCE_USAGE_VERTEX = 0,
    RENDER_RESOURCE_USAGE_UNIFORM = 1
} render_resource_usage_t;

/** Transient resource description. */
typedef struct render_resource {
    render_resource_type_t type;
    uint32_t handle;
    size_t size;
    render_resource_usage_t usage;
} render_resource_t;

/** Resource view kind. */
typedef enum render_resource_view_kind {
    RENDER_RESOURCE_VIEW_ATTACHMENT = 0,
    RENDER_RESOURCE_VIEW_TRANSIENT = 1
} render_resource_view_kind_t;

/** Resource view reference. */
typedef struct render_resource_view {
    render_resource_view_kind_t kind;
    uint32_t index;
} render_resource_view_t;

/** Resource view set for a render pass. */
typedef struct render_resource_view_set {
    const render_resource_attachment_t *attachments;
    size_t attachment_count;
    const render_resource_t *transients;
    size_t transient_count;
    const render_resource_view_t *inputs;
    size_t input_count;
    const render_resource_view_t *outputs;
    size_t output_count;
} render_resource_view_set_t;

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_RENDERER_RENDER_RESOURCE_VIEWS_H */
