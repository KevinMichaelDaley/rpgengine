/**
 * @file camera.c
 * @brief FPS camera controller implementation.
 */

#include "ferrum/demo/camera.h"
#include <math.h>

/* ±89 degrees in radians. */
static const float PITCH_LIMIT = 1.5533f;

void demo_camera_init(demo_camera_t *cam)
{
    cam->position    = (vec3_t){ 0.0f, 1.7f, 0.0f };
    cam->yaw         = 0.0f;
    cam->pitch       = 0.0f;
    cam->move_speed  = 5.0f;
    cam->sensitivity = 0.003f;
}

void demo_camera_update(demo_camera_t *cam, float mouse_dx, float mouse_dy,
                        uint8_t wasd_flags, float dt)
{
    /* Adjust yaw and pitch from mouse input. */
    cam->yaw   -= mouse_dx * cam->sensitivity;
    cam->pitch -= mouse_dy * cam->sensitivity;

    /* Clamp pitch to avoid gimbal-lock region. */
    if (cam->pitch >  PITCH_LIMIT) cam->pitch =  PITCH_LIMIT;
    if (cam->pitch < -PITCH_LIMIT) cam->pitch = -PITCH_LIMIT;

    /* Flat forward and right vectors (no pitch component for movement). */
    float forward_x =  sinf(cam->yaw);
    float forward_z =  cosf(cam->yaw);
    float right_x   =  cosf(cam->yaw);
    float right_z   = -sinf(cam->yaw);

    /* Decode WASD flags: bit 0=W, 1=A, 2=S, 3=D. */
    float forward_amount = 0.0f;
    float right_amount   = 0.0f;
    if (wasd_flags & 0x01) forward_amount += 1.0f; /* W */
    if (wasd_flags & 0x04) forward_amount -= 1.0f; /* S */
    if (wasd_flags & 0x08) right_amount   += 1.0f; /* D */
    if (wasd_flags & 0x02) right_amount   -= 1.0f; /* A */

    float step = cam->move_speed * dt;

    cam->position.x += (forward_x * forward_amount + right_x * right_amount) * step;
    cam->position.z += (forward_z * forward_amount + right_z * right_amount) * step;
}

void demo_camera_view_matrix(const demo_camera_t *cam, mat4_t *out)
{
    vec3_t fwd    = demo_camera_forward(cam);
    vec3_t target = vec3_add(cam->position, fwd);
    vec3_t up     = (vec3_t){ 0.0f, 1.0f, 0.0f };

    mat4_look_at(cam->position, target, up, out);
}

vec3_t demo_camera_forward(const demo_camera_t *cam)
{
    float cos_pitch = cosf(cam->pitch);
    return (vec3_t){
        sinf(cam->yaw) * cos_pitch,
        sinf(cam->pitch),
        cosf(cam->yaw) * cos_pitch
    };
}
