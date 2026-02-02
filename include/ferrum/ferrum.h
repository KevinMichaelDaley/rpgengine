#ifndef FERRUM_FERRUM_H
#define FERRUM_FERRUM_H

/** @file
 * Aggregator header for Ferrum Engine C public APIs.
 */

#include "ferrum/job/system.h"
#include "ferrum/job/counter.h"
#include "ferrum/job/counter.h"
#include "ferrum/math/constants.h"
#include "ferrum/job/instrumentation.h"
#include "ferrum/math/vec2.h"
#include "ferrum/math/vec3.h"
#include "ferrum/math/vec4.h"
#include "ferrum/math/quat.h"
#include "ferrum/math/mat4.h"
#include "ferrum/memory/arena.h"
#include "ferrum/memory/pool.h"
#include "ferrum/memory/apool.h"
#include "ferrum/ecs/common.h"
#include "ferrum/ecs/entity.h"
#include "ferrum/ecs/sparse_set.h"
#include "ferrum/ecs/world.h"
#include "ferrum/renderer/gl_loader.h"
#include "ferrum/renderer/shader_program.h"
#include "ferrum/renderer/vbo.h"
#include "ferrum/renderer/vao_attribute.h"
#include "ferrum/renderer/vao.h"
#include "ferrum/renderer/gl_constants.h"
#include "ferrum/renderer/shader_uniforms.h"
#include "ferrum/renderer/bone_palette.h"
#include "ferrum/renderer/render_resource_views.h"
#include "ferrum/renderer/render_pipeline.h"
#include "ferrum/renderer/render_pipeline_graph.h"
#include "ferrum/renderer/skinning_shader.h"
#include "ferrum/renderer/skinning.h"
#include "ferrum/net/ack_window.h"
#include "ferrum/net/packet_header.h"
#include "ferrum/net/bit_pack.h"
#include "ferrum/net/schema_registry.h"
#include "ferrum/net/unreliable_channel.h"
#include "ferrum/net/reliable_channel.h"
#include "ferrum/net/test_clock.h"
#include "ferrum/net/test_buffer.h"
#include "ferrum/net/test_link.h"

// Client network runtime RX
#include "ferrum/net/client/runtime_rx.h"

#endif /* FERRUM_FERRUM_H */
