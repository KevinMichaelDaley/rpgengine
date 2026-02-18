# Rendering Performance Audit

**Date:** 2025-07-18
**Scope:** `tests/examples/demo_client.c` render loop, `src/renderer/`, `include/ferrum/renderer/`
**Observed:** 200–700+ bodies at ~15–70 ms/frame (draw + SwapWindow)

---

## 1. Current Architecture

### Render Loop Overview (demo_client.c:978–1098)

The render loop is a single-pass immediate-mode renderer:

1. **Clear** (line 980–981): `glClearColor` + `glClear(COLOR|DEPTH)` — once per frame.
2. **Shader bind** (line 983): `shader_program_bind()` → `glUseProgram()` — once per frame.
3. **VAO bind** (line 984): `glBindVertexArray(gl.vao)` — binds the **cube** VAO as the default.
4. **Camera setup** (lines 986–1004): `mat4_perspective`, `mat4_look_at`, VP multiply — CPU-side, once per frame.
5. **Body loop** (lines 1009–1096): For **every active body**:
   - Position/rotation fetch (interpolation or physics pool read)
   - CPU-side model matrix computation: `mat4_translation * mat4_from_quat * mat4_scaling`
   - CPU-side MVP computation: `mat4_mul(vp, model)`
   - **Uniform upload `u_mvp`** via `shader_uniform_set_mat4()` (line 1065)
   - **Uniform upload `u_color`** via `shader_uniform_set_vec3()` (line 1076)
   - **Conditional VAO rebind** for non-cube shapes (lines 1079–1095)
   - **`glDrawArrays(GL_TRIANGLES, ...)`** — one draw call per body

### Per-Body GL Calls

For a **cube** (shape_type 0/1, the most common):
| Call | Count |
|------|-------|
| `shader_uniform_resolve` (strcmp loop × 2) | 2 |
| `glUniformMatrix4fv` | 1 |
| `glUniform3fv` | 1 |
| `glDrawArrays` | 1 |
| **Total GL calls** | **3** |

For a **non-cube shape** (plane, capsule, armadillo):
| Call | Count |
|------|-------|
| Uniform uploads | 2 |
| `glBindVertexArray` (switch to shape VAO) | 1 |
| `glDrawArrays` | 1 |
| `glBindVertexArray` (switch back to cube VAO) | 1 |
| **Total GL calls** | **5** |

### Per-Frame GL Calls (N bodies, M non-cube)

```
glClearColor              ×1
glClear                   ×1
glUseProgram              ×1
glBindVertexArray         ×1 (initial) + 2×M (non-cube switch+restore) + 1 (final unbind)
glUniformMatrix4fv        ×N
glUniform3fv              ×N
glDrawArrays              ×N
─────────────────────────────
Total GL calls:  3N + 2M + 4
```

For **700 bodies** with ~100 non-cube shapes: **~2,304 GL calls/frame**.

---

## 2. Per-Frame Overhead Breakdown

### 2.1 Draw Call Overhead (DOMINANT)

Each `glDrawArrays` call triggers a full GPU command submission. At 700 bodies, that's **700 draw calls per frame**. On desktop GL with a composited desktop, each draw call costs ~5–15 µs of driver overhead, yielding:

- **Conservative estimate:** 700 × 5 µs = **3.5 ms** just in driver overhead
- **Worst case (composited):** 700 × 15 µs = **10.5 ms**

The armadillo mesh is particularly expensive: 212,574 triangles × 3 verts = **637,722 vertices per draw call**. If there are even a few armadillo bodies, each one is rendering more geometry than the rest of the scene combined.

### 2.2 Uniform Upload Overhead (MODERATE)

Every body issues 2 uniform uploads per frame. Each goes through `shader_uniform_resolve()` (shader_uniforms_init.c:49–85) which:

1. Calls `shader_uniform_find()` — **linear `strcmp` scan** of up to 64 cache entries (line 9–10)
2. On cache miss (first frame only): calls `glGetUniformLocation` — slow GL query
3. On cache hit: returns cached `int32_t` location

The cache has only 2 entries (`u_mvp`, `u_color`) so the linear scan is trivially fast (2 iterations max). However, each call still does:
- 4 NULL pointer checks (lines 92–97 for mat4, lines 112–117 for vec3)
- A function call through `shader_uniform_resolve` → `shader_uniform_find`
- A `strcmp` per cache entry

At 700 bodies × 2 uniforms = 1,400 resolve calls. The `strcmp` cost is negligible (~1 µs total), but the **function call overhead and branch mispredictions** through the cache abstraction add up.

**Cost estimate:** ~0.1–0.3 ms/frame (minor)

### 2.3 CPU-side Matrix Math (MODERATE)

Per body (lines 1048–1063):
- `mat4_translation` — 16 float writes
- `mat4_from_quat` → `quat_to_mat4` — 20+ float multiplies
- `mat4_scaling` — 16 float writes
- 2× `mat4_mul` (model = T×R×S, mvp = VP×model) — 128 float multiplies total

At 700 bodies: ~700 × 180 float ops = **126,000 float ops**. On a modern CPU this is ~0.2–0.5 ms. Not dominant, but not free.

### 2.4 VAO Rebinding (MINOR)

Non-cube shapes trigger `glBindVertexArray` twice per body (switch + restore, lines 1080–1092). VAO rebinds are relatively cheap (~1–2 µs each) on modern drivers but the **restore-to-cube pattern** is wasteful:

```c
// For every plane body:
glBindVertexArray(plane_vao);  // switch
glDrawArrays(GL_TRIANGLES, 0, 6);
glBindVertexArray(gl.vao);      // restore to cube — WASTEFUL if next body is also non-cube
```

With 100 non-cube shapes: ~200 extra `glBindVertexArray` calls = **~0.2–0.4 ms**.

### 2.5 Body Pool Iteration (MINOR)

The loop iterates `world.body_pool.capacity` (1024, line 1008–1009), checking `phys_body_pool_is_active()` for each slot. Inactive slots are skipped, but the branch prediction cost for 1024 iterations with ~300–700 active is non-trivial due to random skip patterns.

**Cost estimate:** ~0.05 ms

### 2.6 Cost Summary

| Source | Est. Cost (700 bodies) | % of 70ms |
|--------|----------------------|-----------|
| Draw calls (driver overhead) | 3.5–10.5 ms | 5–15% |
| Armadillo draw (if present) | 5–20 ms (GPU) | 7–29% |
| Uniform uploads | 0.1–0.3 ms | <1% |
| CPU matrix math | 0.2–0.5 ms | <1% |
| VAO rebinding | 0.2–0.4 ms | <1% |
| **SwapWindow / compositor** | **20–50 ms** | **29–71%** |
| **Total** | **~30–80 ms** | |

**Key insight:** The 15–70 ms range strongly suggests **compositor-forced vsync or buffer synchronization** is the dominant cost, not the GL calls themselves.

---

## 3. Instancing Opportunities

### Shape Distribution

Bodies are categorized by `shape_type`:
| shape_type | Mesh | Typical Count | Vertex Count | VAO |
|------------|------|---------------|-------------|-----|
| 0 (box) | Unit cube | 200–600 | 36 | `gl.vao` |
| 1 (sphere) | Unit cube* | 10–50 | 36 | `gl.vao` |
| 2 (capsule) | Generated sphere-ish | 5–20 | ~720 | `gl.cap_vao` |
| 3 (mesh/armadillo) | OBJ loaded | 1–10 | **637,722** | `gl.arm_vao` |
| 4 (halfspace/plane) | Quad | 1–5 | 6 | `gl.plane_vao` |

*Note: sphere (type 1) reuses the cube mesh with uniform scaling — effectively renders as a cube.*

### Instancing Plan

**Boxes (type 0+1):** The overwhelming majority of bodies share the same 36-vertex cube mesh. These are ideal candidates for `glDrawArraysInstanced`:

1. Allocate an instance VBO with per-instance data: `mat4 mvp` + `vec3 color` = 76 bytes/instance
2. Upload all box instance data in a single `glBufferSubData` call
3. Draw with `glDrawArraysInstanced(GL_TRIANGLES, 0, 36, box_count)`

**Expected savings:** Replace 200–600 draw calls + 400–1200 uniform uploads with **1 draw call + 1 buffer upload**. Estimated savings: **2–8 ms/frame**.

**Capsules (type 2):** 5–20 instances. Worth instancing if the infrastructure exists; marginal benefit alone.

**Armadillos (type 3):** 1–10 instances of a 637K-vertex mesh. Instancing saves draw call overhead but the **GPU vertex processing** is the real cost. Each armadillo instance pushes ~637K vertices through the vertex shader. With 5 armadillos, that's **3.2M vertices/frame** for this mesh alone.

**Planes (type 4):** 1–5 instances. Negligible benefit.

### Recommended Instance Batching Order

1. **Boxes first** — highest count, lowest per-instance vertex cost
2. **Capsules second** — moderate count
3. **Armadillos** — low count but massive per-instance cost (consider LOD)
4. **Planes last** — trivial count

---

## 4. Uniform Upload Analysis

### Current Implementation (shader_uniforms_init.c)

```c
// shader_uniform_find — linear scan (line 5–21)
static shader_uniform_status_t shader_uniform_find(
    shader_uniform_cache_t *cache, const char *name,
    uint8_t type, uint32_t *out_index) {
    for (uint32_t i = 0; i < cache->count; ++i) {
        if (cache->entries[i].name != NULL &&
            strcmp(cache->entries[i].name, name) == 0) {  // strcmp per entry!
            ...
        }
    }
}
```

The cache stores `{name, location, type}` tuples and resolves via `strcmp`. With only 2 uniforms (`u_mvp`, `u_color`), the scan is O(2) — effectively free.

### Is It a Bottleneck?

**No, not at the current scale.** The `strcmp` scan with 2 entries adds < 50 ns per call. At 1,400 calls/frame that's ~70 µs — negligible.

### But the Abstraction Has Hidden Costs

Each `shader_uniform_set_mat4` call (shader_uniforms_init.c:87–106):
1. NULL check cache, program, name, value (4 branches)
2. NULL check `glUniformMatrix4fv` (1 branch)
3. Call `shader_uniform_resolve` → `shader_uniform_find` (function call + loop + strcmp)
4. Check if location is cached, potentially call `glGetUniformLocation` (branch)
5. Finally: `cache->glUniformMatrix4fv(location, 1, transpose, value)` (indirect call through function pointer)

That's **6+ branches + 2 function calls + 1 indirect call** per uniform, versus the bare minimum:

```c
// Direct approach — what the driver actually needs:
glUniformMatrix4fv(mvp_loc, 1, GL_FALSE, mvp.m);
glUniform3fv(color_loc, 1, rgb);
```

### Recommendation

For the hot loop, resolve uniform locations **once at init** and use direct `glUniform*` calls:

```c
// At init:
GLint u_mvp_loc = glGetUniformLocation(program, "u_mvp");
GLint u_color_loc = glGetUniformLocation(program, "u_color");

// In loop:
glUniformMatrix4fv(u_mvp_loc, 1, GL_FALSE, mvp.m);
glUniform3fv(u_color_loc, 1, rgb);
```

**Expected savings:** ~0.1 ms/frame. Minor, but trivial to implement.

---

## 5. VAO Rebinding Analysis

### Current Pattern (demo_client.c:1079–1095)

```c
if (ri->shape_type == 4) {
    glBindVertexArray(vao_handle(&gl.plane_vao));   // line 1080
    glDrawArrays(GL_TRIANGLES, 0, 6);               // line 1081
    glBindVertexArray(vao_handle(&gl.vao));          // line 1082 — RESTORE
} else if (ri->shape_type == 3 && gl.arm_vert_count > 0) {
    glBindVertexArray(vao_handle(&gl.arm_vao));      // line 1084
    glDrawArrays(GL_TRIANGLES, 0, arm_count);        // line 1085–1086
    glBindVertexArray(vao_handle(&gl.vao));           // line 1087 — RESTORE
} else if (ri->shape_type == 2) {
    glBindVertexArray(vao_handle(&gl.cap_vao));      // line 1089
    glDrawArrays(GL_TRIANGLES, 0, cap_count);        // line 1090–1091
    glBindVertexArray(vao_handle(&gl.vao));           // line 1092 — RESTORE
} else {
    glDrawArrays(GL_TRIANGLES, 0, 36);               // line 1094 — cube
}
```

### Issues

1. **Immediate restore to cube VAO** after every non-cube draw. If the next body is also non-cube (e.g., two capsules in a row), this causes an unnecessary bind-away-and-back.

2. **`vao_handle()` is a function call** (vao_handle.c:3–8) that does a NULL check on every invocation. With multiple calls per body, this adds trivial but unnecessary overhead.

3. **No sorting by shape type.** Bodies are iterated in pool index order, meaning VAO switches happen based on spawn order, not geometry type. Sorting by `shape_type` would cluster same-VAO draws together.

### Cost

VAO rebinds on modern GL drivers (Mesa, NVIDIA) cost ~0.5–2 µs each. With 100 non-cube bodies × 2 binds each = 200 binds ≈ **0.1–0.4 ms**.

### Recommendation

Sort bodies by shape_type before rendering, or batch draws per shape (see instancing). Even without instancing, drawing all cubes → all capsules → all armadillos → all planes eliminates redundant VAO switches and reduces binds from 2×M to just 4 (one per shape type).

---

## 6. Compositor / VSync Analysis

### Current Configuration

```c
SDL_GL_SetSwapInterval(0);  // demo_client.c:404
```

This requests **no vsync** from SDL/GL. However:

### Desktop Compositor Override

On Linux with Wayland or composited X11 (GNOME, KDE with effects, etc.), the **compositor may force triple buffering or vsync regardless of the application's request**:

- **Wayland:** Always composited. `SDL_GL_SetSwapInterval(0)` is often ignored; the compositor applies its own buffer management.
- **X11 + compositor (Mutter, KWin):** The compositor typically triple-buffers all windows. `SwapWindow` may block until the compositor is ready.
- **X11 unredirected (fullscreen):** Bypasses compositor; `SetSwapInterval(0)` is honored.

### Evidence

The 15–70 ms range with 200–700 bodies suggests **SwapWindow is blocking for compositor frame delivery**, not GPU work:

- If the bottleneck were GPU draw calls, frame time would scale linearly with body count.
- The wide range (15–70 ms) and the floor at ~15 ms (≈66 Hz) matches compositor-forced vsync artifacts.
- Simple unlit/untextured 36-vertex cubes at 700 instances should render in < 1 ms on any modern GPU.

### Mitigations

| Mitigation | Expected Impact | Effort |
|-----------|----------------|--------|
| Run fullscreen (`SDL_WINDOW_FULLSCREEN`) | Bypasses compositor on X11; may work on some Wayland compositors | Low |
| Set `SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR` to `"1"` | Forces X11 compositor bypass (X11 only) | Trivial |
| Set env `__GL_SYNC_TO_VBLANK=0` (NVIDIA) | Disables vsync at driver level | Trivial |
| Set env `vblank_mode=0` (Mesa) | Disables vsync at driver level | Trivial |
| Use `SDL_GL_SetSwapInterval(-1)` (adaptive vsync) | Swaps immediately if frame is late | Trivial |
| Use Vulkan instead of GL | Finer-grained swap chain control | High |

### Recommended Immediate Fix (demo_client.c)

```c
// After SDL_Init, before window creation:
SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "1");

// Or fullscreen:
ctx->window = SDL_CreateWindow("demo_client", ...,
    SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_FULLSCREEN_DESKTOP);
```

---

## 7. Other Issues

### 7.1 No Back-Face Culling (CRITICAL)

```c
glEnable(GL_DEPTH_TEST);   // demo_client.c:403
// Missing: glEnable(GL_CULL_FACE);
```

**Every triangle is rasterized from both sides.** For closed convex meshes (cubes, capsules, armadillos), the back faces are always hidden by front faces but still processed through the fragment shader. This **doubles the fragment workload**.

For the armadillo (212,574 tris), enabling `GL_CULL_FACE` eliminates ~106K triangles per instance.

**Fix (1 line):**
```c
glEnable(GL_CULL_FACE);  // Add after line 403
```

**Expected savings:** ~30–50% reduction in fragment shader work. For armadillo-heavy scenes, this could save several ms.

### 7.2 No Frustum Culling

The render loop draws **every active body** regardless of whether it's visible. With a 70° FOV, roughly 50–70% of bodies may be off-screen at any time.

Bodies behind the camera, far outside the view, or occluded by other geometry are still processed through the full per-body path: matrix math → uniform upload → draw call.

**Recommendation:** Implement simple frustum culling using the 6 frustum planes extracted from the VP matrix. For each body, test its bounding sphere against the frustum. Skip `glDrawArrays` for bodies that fail.

**Expected savings:** 30–60% reduction in draw calls, depending on camera FOV and scene layout. At 700 bodies, this could eliminate 200–400 draw calls ≈ **1–4 ms**.

### 7.3 Sphere Uses Cube Mesh

Shape type 1 (sphere) renders using the cube VAO with no distinction from boxes:

```c
} else {
    glDrawArrays(GL_TRIANGLES, 0, 36);  // line 1094 — cube for type 0 AND 1
}
```

This means spheres render as cubes. If this is intentional (placeholder), it's fine. If spheres should look round, they need their own mesh (or reuse the capsule mesh with uniform scale).

### 7.4 Armadillo Mesh Is Enormous

The armadillo OBJ has **212,574 triangles** (637,722 vertices). This is a high-poly mesh being rendered with no:
- Level-of-detail (LOD) system
- Index buffer (using `glDrawArrays` = no vertex reuse via index buffer)
- Mesh simplification

**Memory impact:** 637,722 verts × 3 floats × 4 bytes = **7.65 MB** per armadillo VBO. With indexed rendering and a typical OBJ sharing factor of ~6×, this could be reduced to ~1.3 MB.

**Draw impact:** Each armadillo draw pushes 637K vertices through the vertex shader. With instancing, the vertex data is read once per instance, but without it, each draw call resubmits all vertex data.

### 7.5 No Index Buffer Usage

All meshes use `glDrawArrays` (non-indexed). For the cube (36 verts from 8 unique positions), indexed rendering would reduce vertex shader invocations from 36 to 8 (4.5× reduction). For the armadillo, the savings would be even more dramatic.

**Recommendation:** Convert to indexed rendering (`glDrawElements`) with element buffers. Lower priority than instancing.

### 7.6 Shader Does No Lighting

The fragment shader is trivial:
```glsl
void main() { out_color = vec4(u_color, 1.0); }  // demo_client.c:299
```

This is a flat-color shader with no lighting, normals, or material properties. The GPU fragment cost per pixel is minimal (~1 cycle). This means **the bottleneck is NOT the fragment shader** — it's draw call overhead, vertex processing (armadillo), and compositor stalls.

### 7.7 glClearColor Called Every Frame

```c
glClearColor(0.05f, 0.05f, 0.08f, 1.0f);  // line 980 — same color every frame
glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);  // line 981
```

`glClearColor` sets GL state that persists until changed. Calling it every frame with the same values is a redundant state change. Minor (~1 µs), but easy to fix by moving it to `gl_init()`.

### 7.8 Body Pool Iteration Is Not Cache-Friendly

The render loop accesses `render_info[i]` and `world.body_pool.bodies_curr[i]` for each active body, iterating up to `capacity` (1024). The `body_render_info_t` is 16 bytes, so 1024 entries fit in ~16 KB (L1 cache). The physics bodies are likely larger and may cause L2/L3 cache misses for cold entries.

---

## 8. Prioritized Recommendations

### Quick Wins (< 1 hour each)

| # | Change | File:Line | Expected Impact | Effort |
|---|--------|-----------|----------------|--------|
| 1 | **Enable back-face culling** | demo_client.c:403 | -30–50% fragment work | 1 line |
| 2 | **Bypass compositor** | demo_client.c:245 | -20–50 ms if compositor is forcing vsync | 1 line |
| 3 | **Move glClearColor to init** | demo_client.c:980→ 403 | Negligible, but cleaner | 1 line |
| 4 | **Cache uniform locations directly** | demo_client.c:1065–1077 | -0.1 ms, simpler hot path | ~10 lines |
| 5 | **Sort by shape type before draw** | demo_client.c:1009 | -0.2–0.4 ms (fewer VAO rebinds) | ~30 lines |

### Medium Effort (1–4 hours)

| # | Change | Expected Impact | Effort |
|---|--------|----------------|--------|
| 6 | **Instanced rendering for cubes** | -2–8 ms (eliminate 200–600 draw calls) | ~200 lines |
| 7 | **Frustum culling** | -1–4 ms (skip 30–60% of bodies) | ~100 lines |
| 8 | **Eliminate VAO restore-to-cube pattern** | -0.1–0.4 ms | ~20 lines |

### Larger Effort (4+ hours)

| # | Change | Expected Impact | Effort |
|---|--------|----------------|--------|
| 9 | **Indexed rendering** | -30–80% vertex shader invocations | ~300 lines |
| 10 | **Armadillo LOD / mesh simplification** | Massive for armadillo-heavy scenes | Variable |
| 11 | **Instance batching for all shape types** | Reduces total draw calls to ~4 | ~400 lines |
| 12 | **GPU-driven rendering (SSBO + compute)** | Near-zero CPU overhead per body | ~800 lines |

### Expected Combined Impact

Implementing recommendations 1–7 should reduce frame time from **15–70 ms** to **2–5 ms** for 700 bodies, achieving **150–500 FPS** on a modern GPU. The largest single wins are compositor bypass (#2) and instanced rendering (#6).

---

## Appendix: Render System File Map

| File | Purpose | Key Functions |
|------|---------|---------------|
| `tests/examples/demo_client.c:978–1098` | Main render loop | Body iteration, draw calls |
| `tests/examples/demo_client.c:245–408` | GL init | Window, shader, VAO/VBO setup |
| `src/renderer/shader_uniforms_init.c` | Uniform cache + setters | `shader_uniform_resolve`, `shader_uniform_find` |
| `src/renderer/shader_program_bind.c` | Program binding | `shader_program_bind` |
| `src/renderer/shader_program_create.c` | Shader compilation | `shader_program_create` |
| `src/renderer/vao_create.c` | VAO creation | `vao_create` |
| `src/renderer/vao_bind_attributes.c` | VAO attribute setup | `vao_bind_attributes` |
| `src/renderer/vao_handle.c` | VAO handle accessor | `vao_handle` |
| `src/renderer/vbo_upload.c` | VBO data upload | `vbo_upload` |
| `include/ferrum/renderer/shader_uniforms.h` | Uniform cache types | `shader_uniform_cache_t` (64-entry linear cache) |
| `include/ferrum/renderer/shader_program.h` | Program type | `shader_program_t` (stores all GL function pointers) |
| `include/ferrum/renderer/vao.h` | VAO type | `vao_t` (stores GL function pointers) |
| `include/ferrum/renderer/vbo.h` | VBO type | `vbo_t` (stores GL function pointers) |
| `include/ferrum/renderer/render_pipeline.h` | Pipeline abstraction | `render_pipeline_t` (not used by demo_client) |
