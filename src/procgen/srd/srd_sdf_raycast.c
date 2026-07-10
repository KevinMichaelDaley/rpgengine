/**
 * @file srd_sdf_raycast.c
 * @brief CPU SDF raymarcher with point-light shadows.
 *
 * For each pixel: 2x2 supersample, sphere-trace primary ray (with
 * sign-change bisection), compute normal, then for each point light
 * sphere-trace a shadow ray and accumulate diffuse + specular.
 * Average the 4 subpixel samples to filter SDF contour bands.
 *
 * Non-static functions (3): srd_raycast_config_default, srd_sdf_raycast,
 *                            srd_sdf_sample
 */
#include "ferrum/procgen/srd/srd_sdf_raycast.h"

#include <math.h>
#include <string.h>

/* ── Trilinear SDF sampling ──────────────────────────────────── */

static int clampi(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static float grid_val(const srd_sdf_grid_t *g, int x, int y, int z) {
    x = clampi(x, 0, g->nx - 1);
    y = clampi(y, 0, g->ny - 1);
    z = clampi(z, 0, g->nz - 1);
    return g->values[z * g->ny * g->nx + y * g->nx + x];
}

float srd_sdf_sample(const srd_sdf_grid_t *grid,
                     float wx, float wy, float wz) {
    if (!grid || !grid->values) return SRD_SDF_OUTSIDE;

    float fx = (wx - grid->origin[0]) / grid->voxel_size;
    float fy = (wy - grid->origin[1]) / grid->voxel_size;
    float fz = (wz - grid->origin[2]) / grid->voxel_size;

    if (fx < -0.5f || fx > (float)(grid->nx - 1) + 0.5f ||
        fy < -0.5f || fy > (float)(grid->ny - 1) + 0.5f ||
        fz < -0.5f || fz > (float)(grid->nz - 1) + 0.5f)
        return SRD_SDF_OUTSIDE;

    int x0 = (int)floorf(fx), y0 = (int)floorf(fy), z0 = (int)floorf(fz);
    float tx = fx - (float)x0;
    float ty = fy - (float)y0;
    float tz = fz - (float)z0;

    float c000 = grid_val(grid, x0,   y0,   z0);
    float c100 = grid_val(grid, x0+1, y0,   z0);
    float c010 = grid_val(grid, x0,   y0+1, z0);
    float c110 = grid_val(grid, x0+1, y0+1, z0);
    float c001 = grid_val(grid, x0,   y0,   z0+1);
    float c101 = grid_val(grid, x0+1, y0,   z0+1);
    float c011 = grid_val(grid, x0,   y0+1, z0+1);
    float c111 = grid_val(grid, x0+1, y0+1, z0+1);

    float c00 = c000 * (1.0f - tx) + c100 * tx;
    float c10 = c010 * (1.0f - tx) + c110 * tx;
    float c01 = c001 * (1.0f - tx) + c101 * tx;
    float c11 = c011 * (1.0f - tx) + c111 * tx;

    float c0 = c00 * (1.0f - ty) + c10 * ty;
    float c1 = c01 * (1.0f - ty) + c11 * ty;

    float result = c0 * (1.0f - tz) + c1 * tz;
    /* Fixed cap in physical units so sphere tracer takes large steps
     * through empty space regardless of grid resolution. */
    return clampf(result, -2.5f, 2.5f);
}

/* ── Vector helpers ──────────────────────────────────────────── */

static void normalize3(float *v) {
    float len = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (len < 1e-8f) return;
    float inv = 1.0f / len;
    v[0] *= inv; v[1] *= inv; v[2] *= inv;
}

static void cross3(const float *a, const float *b, float *out) {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

/* ── SDF normal (tetrahedron + axis snap) ────────────────────── */

static float copysignf_safe(float mag, float sign) {
    return sign >= 0.0f ? mag : -mag;
}

static void sdf_normal(const srd_sdf_grid_t *grid,
                       float wx, float wy, float wz,
                       float *nx, float *ny, float *nz) {
    float eps = grid->voxel_size * 0.5f;
    float k[4][3] = {
        { 1, -1, -1}, { -1, -1, 1}, { -1, 1, -1}, { 1, 1, 1}
    };
    float gx = 0.0f, gy = 0.0f, gz = 0.0f;
    for (int i = 0; i < 4; i++) {
        float s = srd_sdf_sample(grid,
            wx + eps * k[i][0], wy + eps * k[i][1], wz + eps * k[i][2]);
        gx += k[i][0] * s;
        gy += k[i][1] * s;
        gz += k[i][2] * s;
    }
    float len = sqrtf(gx * gx + gy * gy + gz * gz);
    if (len < 1e-8f) { *nx = 0.0f; *ny = 1.0f; *nz = 0.0f; return; }
    float inv = 1.0f / len;
    float rx = gx * inv, ry = gy * inv, rz = gz * inv;

    /* Axis snap: on flat surfaces the normal should be exactly
     * axis-aligned. CSG interference from overlapping box SDFs
     * perturbs it, creating visible contour bands. Snap the
     * normal toward the nearest axis when it's nearly flat,
     * preserving raw gradient on edges/corners. */
    float ax = fabsf(rx), ay = fabsf(ry), az = fabsf(rz);
    float dominant = ax;
    if (ay > dominant) dominant = ay;
    if (az > dominant) dominant = az;

    /* snap_factor: 0 at dominant=0.7 (45° edge), 1 at dominant=0.9+ */
    float snap = clampf((dominant - 0.7f) / 0.2f, 0.0f, 1.0f);
    snap *= snap;

    float sx = 0.0f, sy = 0.0f, sz = 0.0f;
    if (ax >= ay && ax >= az)      sx = copysignf_safe(1.0f, rx);
    else if (ay >= ax && ay >= az) sy = copysignf_safe(1.0f, ry);
    else                           sz = copysignf_safe(1.0f, rz);

    float bx = rx * (1.0f - snap) + sx * snap;
    float by = ry * (1.0f - snap) + sy * snap;
    float bz = rz * (1.0f - snap) + sz * snap;

    float blen = sqrtf(bx * bx + by * by + bz * bz);
    if (blen < 1e-8f) { *nx = rx; *ny = ry; *nz = rz; return; }
    float binv = 1.0f / blen;
    *nx = bx * binv; *ny = by * binv; *nz = bz * binv;
}

/* ── SDF ambient occlusion ───────────────────────────────────── */

static float sdf_ao(const srd_sdf_grid_t *grid,
                    const float *pos, const float *nor) {
    float occ = 0.0f;
    float scale = 1.0f;
    for (int i = 0; i < 5; i++) {
        float h = 0.02f + 0.15f * (float)i;
        float d = fabsf(srd_sdf_sample(grid,
            pos[0] + h * nor[0], pos[1] + h * nor[1], pos[2] + h * nor[2]));
        occ += (h - d) * scale;
        scale *= 0.6f;
    }
    return clampf(1.0f - 3.0f * occ, 0.0f, 1.0f);
}

/* ── Sphere-traced soft shadow toward a point light ──────────── */

static float sdf_shadow_point(const srd_sdf_grid_t *grid,
                              const float *ro, const float *lpos,
                              float k) {
    float dir[3];
    dir[0] = lpos[0] - ro[0];
    dir[1] = lpos[1] - ro[1];
    dir[2] = lpos[2] - ro[2];
    float max_t = sqrtf(dir[0]*dir[0] + dir[1]*dir[1] + dir[2]*dir[2]);
    if (max_t < 1e-6f) return 1.0f;
    float inv_max = 1.0f / max_t;
    dir[0] *= inv_max; dir[1] *= inv_max; dir[2] *= inv_max;

    float res = 1.0f;
    float ms = grid->voxel_size * 0.1f;
    float t = grid->voxel_size * 0.5f;

    for (int i = 0; i < 128 && t < max_t; i++) {
        float d = srd_sdf_sample(grid,
            ro[0] + t * dir[0], ro[1] + t * dir[1], ro[2] + t * dir[2]);
        float ad = fabsf(d);

        if (d > 0.0f && ad < grid->voxel_size * 0.5f)
            return 0.0f;

        float s = k * ad / t;
        if (s < res) res = s;

        /* Conservative step: 0.7x SDF value to avoid overshooting */
        float step = ad * 0.7f;
        t += step > ms ? step : ms;
    }
    return clampf(res, 0.0f, 1.0f);
}

/* ── AABB ray intersection ───────────────────────────────────── */

static int ray_aabb(const float *ro, const float *rd,
                    const float *bmin, const float *bmax,
                    float *t_near, float *t_far) {
    float tmin = -1e30f, tmax = 1e30f;
    for (int i = 0; i < 3; i++) {
        if (fabsf(rd[i]) < 1e-12f) {
            if (ro[i] < bmin[i] || ro[i] > bmax[i]) return 0;
        } else {
            float inv = 1.0f / rd[i];
            float t1 = (bmin[i] - ro[i]) * inv;
            float t2 = (bmax[i] - ro[i]) * inv;
            if (t1 > t2) { float tmp = t1; t1 = t2; t2 = tmp; }
            if (t1 > tmin) tmin = t1;
            if (t2 < tmax) tmax = t2;
            if (tmin > tmax) return 0;
        }
    }
    *t_near = tmin;
    *t_far = tmax;
    return 1;
}

/* ── Per-pixel shading ───────────────────────────────────────── */

static void shade_pixel(const srd_sdf_grid_t *grid,
                        const srd_raycast_config_t *cfg,
                        const float *hit, const float *rd,
                        float dist,
                        float *out_r, float *out_g, float *out_b) {
    float n[3];
    sdf_normal(grid, hit[0], hit[1], hit[2], &n[0], &n[1], &n[2]);

    if (n[0]*rd[0] + n[1]*rd[1] + n[2]*rd[2] > 0.0f) {
        n[0] = -n[0]; n[1] = -n[1]; n[2] = -n[2];
    }

    float ao = sdf_ao(grid, hit, n);

    float bias = grid->voxel_size * 3.0f;
    float sro[3];
    sro[0] = hit[0] + n[0] * bias;
    sro[1] = hit[1] + n[1] * bias;
    sro[2] = hit[2] + n[2] * bias;

    float mat_r = 0.70f, mat_g = 0.63f, mat_b = 0.52f;

    float lr = 0.0f, lg = 0.0f, lb = 0.0f;

    for (int li = 0; li < cfg->n_lights && li < SRD_MAX_LIGHTS; li++) {
        const srd_point_light_t *lt = &cfg->lights[li];

        float ld[3];
        ld[0] = lt->pos[0] - hit[0];
        ld[1] = lt->pos[1] - hit[1];
        ld[2] = lt->pos[2] - hit[2];
        float d2 = ld[0]*ld[0] + ld[1]*ld[1] + ld[2]*ld[2];
        float ldist = sqrtf(d2);
        if (ldist < 1e-6f) continue;
        float inv_ld = 1.0f / ldist;
        ld[0] *= inv_ld; ld[1] *= inv_ld; ld[2] *= inv_ld;

        /* Inverse-square falloff: radius is luminous power.
         * atten = radius / (d² + 1). Physical d² decay with
         * singularity guard at d=0 and radius as intensity scale. */
        float atten = lt->radius / (d2 + 1.0f);

        float sha = sdf_shadow_point(grid, sro, lt->pos, 10.0f);

        float ndl = n[0]*ld[0] + n[1]*ld[1] + n[2]*ld[2];
        float diff = clampf(ndl, 0.0f, 1.0f);

        float hv[3];
        hv[0] = ld[0] - rd[0]; hv[1] = ld[1] - rd[1]; hv[2] = ld[2] - rd[2];
        normalize3(hv);
        float ndh = n[0]*hv[0] + n[1]*hv[1] + n[2]*hv[2];
        ndh = clampf(ndh, 0.0f, 1.0f);
        float spec = powf(ndh, 32.0f);

        float illum = atten * sha;

        lr += lt->color[0] * (mat_r * diff + 0.15f * spec) * illum;
        lg += lt->color[1] * (mat_g * diff + 0.12f * spec) * illum;
        lb += lt->color[2] * (mat_b * diff + 0.10f * spec) * illum;
    }

    float amb = cfg->ambient * ao;
    lr += mat_r * amb;
    lg += mat_g * amb;
    lb += mat_b * amb;

    /* Exponential distance fog */
    float fog_t = dist * 0.06f;
    float fog = expf(-fog_t * fog_t);
    lr = lr * fog + 0.03f * (1.0f - fog);
    lg = lg * fog + 0.03f * (1.0f - fog);
    lb = lb * fog + 0.04f * (1.0f - fog);

    /* Gamma */
    *out_r = powf(clampf(lr, 0.0f, 1.0f), 0.4545f);
    *out_g = powf(clampf(lg, 0.0f, 1.0f), 0.4545f);
    *out_b = powf(clampf(lb, 0.0f, 1.0f), 0.4545f);
}

/* ── Primary ray march ───────────────────────────────────────── */

static int march_ray(const srd_sdf_grid_t *grid,
                     const float *ro, const float *rd,
                     const float *bmin, const float *bmax,
                     int max_steps, float eps, float *out_t) {
    float t_near, t_far;
    if (!ray_aabb(ro, rd, bmin, bmax, &t_near, &t_far))
        return 0;

    float t = t_near > 0.0f ? t_near : 0.0f;
    float ms = grid->voxel_size * 0.1f;

    float prev_d = srd_sdf_sample(grid,
        ro[0] + t * rd[0], ro[1] + t * rd[1], ro[2] + t * rd[2]);
    float prev_t = t;

    for (int step = 0; step < max_steps && t < t_far; step++) {
        float d = srd_sdf_sample(grid,
            ro[0] + t * rd[0], ro[1] + t * rd[1], ro[2] + t * rd[2]);
        float ad = fabsf(d);

        if (ad < eps) { *out_t = t; return 1; }

        if ((prev_d < 0.0f) != (d < 0.0f)) {
            float lo = prev_t, hi = t;
            for (int bi = 0; bi < 12; bi++) {
                float mid = 0.5f * (lo + hi);
                float md = srd_sdf_sample(grid,
                    ro[0] + mid * rd[0], ro[1] + mid * rd[1],
                    ro[2] + mid * rd[2]);
                if ((prev_d < 0.0f) != (md < 0.0f)) hi = mid;
                else lo = mid;
            }
            *out_t = 0.5f * (lo + hi);
            return 1;
        }

        prev_d = d;
        prev_t = t;

        /* Conservative step: 0.7x SDF to avoid overshooting thin features */
        float step_size = ad * 0.7f;
        t += step_size > ms ? step_size : ms;
    }
    return 0;
}

/* ── Public API ──────────────────────────────────────────────── */

void srd_raycast_config_default(srd_raycast_config_t *cfg) {
    if (!cfg) return;
    memset(cfg, 0, sizeof(*cfg));

    cfg->cam_pos[0] = -5.0f;
    cfg->cam_pos[1] = 2.0f;
    cfg->cam_pos[2] = -5.0f;
    cfg->cam_dir[0] = 0.577f;
    cfg->cam_dir[1] = -0.231f;
    cfg->cam_dir[2] = 0.577f;
    cfg->cam_up[0] = 0.0f;
    cfg->cam_up[1] = 1.0f;
    cfg->cam_up[2] = 0.0f;
    cfg->fov_y = 1.047f;
    cfg->width = 128;
    cfg->height = 128;
    cfg->ambient = 0.15f;
    cfg->max_steps = 256;
    cfg->hit_epsilon = 0.001f;
    cfg->n_lights = 0;
}

void srd_sdf_raycast(const srd_sdf_grid_t *grid,
                     const srd_raycast_config_t *cfg,
                     uint8_t *rgb_out) {
    if (!cfg || !rgb_out) return;

    int w = cfg->width;
    int h = cfg->height;

    for (int i = 0; i < w * h; i++) {
        rgb_out[i * 3 + 0] = 30;
        rgb_out[i * 3 + 1] = 30;
        rgb_out[i * 3 + 2] = 35;
    }
    if (!grid || !grid->values) return;

    float fwd[3] = {cfg->cam_dir[0], cfg->cam_dir[1], cfg->cam_dir[2]};
    normalize3(fwd);
    float right[3]; cross3(fwd, cfg->cam_up, right); normalize3(right);
    float up[3]; cross3(right, fwd, up); normalize3(up);

    float half_h = tanf(cfg->fov_y * 0.5f);
    float half_w = half_h * (float)w / (float)h;

    float bmin[3], bmax[3];
    for (int i = 0; i < 3; i++) {
        bmin[i] = grid->origin[i];
        bmax[i] = grid->origin[i];
    }
    bmax[0] += (float)grid->nx * grid->voxel_size;
    bmax[1] += (float)grid->ny * grid->voxel_size;
    bmax[2] += (float)grid->nz * grid->voxel_size;

    /* 2x2 subpixel offsets for supersampling */
    float ss_off[4][2] = {
        {-0.25f, -0.25f}, { 0.25f, -0.25f},
        {-0.25f,  0.25f}, { 0.25f,  0.25f}
    };

    for (int py = 0; py < h; py++) {
        for (int px = 0; px < w; px++) {
            float acc_r = 0.0f, acc_g = 0.0f, acc_b = 0.0f;
            int hits = 0;

            for (int si = 0; si < 4; si++) {
                float sx = (float)px + 0.5f + ss_off[si][0];
                float sy = (float)py + 0.5f + ss_off[si][1];

                float u = (2.0f * sx / (float)w - 1.0f) * half_w;
                float v = (1.0f - 2.0f * sy / (float)h) * half_h;

                float rd[3];
                rd[0] = fwd[0] + u * right[0] + v * up[0];
                rd[1] = fwd[1] + u * right[1] + v * up[1];
                rd[2] = fwd[2] + u * right[2] + v * up[2];
                normalize3(rd);

                float hit_t;
                if (march_ray(grid, cfg->cam_pos, rd, bmin, bmax,
                              cfg->max_steps, cfg->hit_epsilon, &hit_t)) {
                    float p[3];
                    p[0] = cfg->cam_pos[0] + hit_t * rd[0];
                    p[1] = cfg->cam_pos[1] + hit_t * rd[1];
                    p[2] = cfg->cam_pos[2] + hit_t * rd[2];

                    float r, g, b;
                    shade_pixel(grid, cfg, p, rd, hit_t, &r, &g, &b);
                    acc_r += r; acc_g += g; acc_b += b;
                    hits++;
                }
            }

            if (hits > 0) {
                float inv = 1.0f / (float)hits;
                /* Blend with background for partial-coverage pixels */
                float bg_r = 30.0f / 255.0f, bg_g = 30.0f / 255.0f;
                float bg_b = 35.0f / 255.0f;
                float coverage = (float)hits / 4.0f;
                float fr = acc_r * inv * coverage + bg_r * (1.0f - coverage);
                float fg = acc_g * inv * coverage + bg_g * (1.0f - coverage);
                float fb = acc_b * inv * coverage + bg_b * (1.0f - coverage);

                int idx = (py * w + px) * 3;
                rgb_out[idx + 0] = (uint8_t)(fr * 255.0f);
                rgb_out[idx + 1] = (uint8_t)(fg * 255.0f);
                rgb_out[idx + 2] = (uint8_t)(fb * 255.0f);
            }
        }
    }
}
