/**
 * @file probe_brick_place.c
 * @brief SDF-driven ternary brick probe placement (see probe_brick.h).
 *
 * Two passes over the identical deterministic traversal (coarse cells in
 * z,y,x order; children in k,j,i order): pass 1 counts kept bricks so the
 * output arrays can be carved exactly once from the arena; pass 2 emits
 * bricks and probes, deduplicating coincident lattice points through an
 * open-addressing hash on quantized coordinates. The traversal prunes on the
 * keep test itself: |sdf(centre)| > half-diagonal means no point of the brick
 * touches the surface (SDF is 1-Lipschitz), so its children cannot pass either.
 */
#include <math.h>
#include <string.h>

#include "ferrum/memory/arena.h"
#include "ferrum/probe/place/probe_brick.h"

/* Quantized-position hash entry: EMPTY_KEY marks a free slot. */
#define BRICK_HASH_EMPTY UINT64_MAX

typedef struct brick_hash {
    uint64_t *keys;     /* packed quantized coords, BRICK_HASH_EMPTY = free. */
    uint32_t *values;   /* probe index for the key. */
    uint32_t  mask;     /* capacity - 1 (capacity is a power of two). */
} brick_hash_t;

/* Traversal context shared by the count and emit passes. */
typedef struct brick_ctx {
    const probe_brick_config_t *cfg;
    float          half_diag_scale;  /* sqrt(3)/2: half-diagonal = size * this. */
    float          quant;            /* dedup quantum: finest probe spacing / 2. */
    /* emit-pass state (NULL/unused during the count pass): */
    probe_brick_t *bricks;
    uint32_t       n_bricks;
    float         *positions;
    uint32_t       n_probes;
    brick_hash_t   hash;
    /* count-pass result: */
    uint32_t       count;
} brick_ctx_t;

/* Pack quantized (non-negative) lattice coordinates into a hash key. */
static uint64_t brick_key(const brick_ctx_t *c, const float p[3])
{
    uint64_t k = 0;
    for (int a = 0; a < 3; ++a) {
        /* +8 keeps coordinates positive for points slightly under aabb_min. */
        int32_t q = (int32_t)floorf((p[a] - c->cfg->aabb_min[a]) / c->quant + 0.5f) + 8;
        k = (k << 21) | (uint64_t)(uint32_t)(q & 0x1fffff);
    }
    return k;
}

/* Find-or-insert a probe position; returns its index in c->positions. */
static uint32_t brick_probe_intern(brick_ctx_t *c, const float p[3])
{
    uint64_t key = brick_key(c, p);
    uint32_t slot = (uint32_t)(key * 0x9e3779b97f4a7c15ull >> 33) & c->hash.mask;
    for (;;) {
        if (c->hash.keys[slot] == BRICK_HASH_EMPTY) {
            uint32_t idx = c->n_probes++;
            c->hash.keys[slot] = key;
            c->hash.values[slot] = idx;
            memcpy(&c->positions[(size_t)idx * 3u], p, 3 * sizeof(float));
            return idx;
        }
        if (c->hash.keys[slot] == key) return c->hash.values[slot];
        slot = (slot + 1) & c->hash.mask;
    }
}

/* Emit one kept brick: record it and intern its 4x4x4 probe lattice. */
static void brick_emit(brick_ctx_t *c, const float min[3], float size, int level)
{
    probe_brick_t *b = &c->bricks[c->n_bricks++];
    memcpy(b->min, min, sizeof b->min);
    b->size = size;
    b->level = (int32_t)level;
    float step = size / 3.0f;
    for (int k = 0; k < 4; ++k)
        for (int j = 0; j < 4; ++j)
            for (int i = 0; i < 4; ++i) {
                float p[3] = { min[0] + step * (float)i, min[1] + step * (float)j,
                               min[2] + step * (float)k };
                b->probe_idx[(k * 4 + j) * 4 + i] = brick_probe_intern(c, p);
            }
}

/* Depth-first traversal (depth <= PROBE_BRICK_MAX_LEVELS). @p emit selects the
 * pass: 0 = count kept bricks into c->count, 1 = emit bricks + probes.
 *
 * Returns 1 when this brick's volume is COVERED without help from its parent:
 * the brick descends (geometry near), and every sub-volume is either emitted,
 * solid (buried-culled -- nothing to light inside), or covered by descendants.
 * A parent whose 27 children all report covered is SUPPRESSED: the voxel index
 * would never reference it, so emitting it would only waste probes under its
 * own refinement (the coarse-under-fine overlap). Emission order is therefore
 * NOT ancestors-first -- the index build resolves overlap by LEVEL, not order. */
static int brick_descend(brick_ctx_t *c, const float min[3], float size,
                         int level, int emit)
{
    float half = size * 0.5f;
    float centre[3] = { min[0] + half, min[1] + half, min[2] + half };
    float sd = c->cfg->sdf(centre, c->cfg->sdf_user);
    int keep = fabsf(sd) <= size * c->half_diag_scale;

    if (!keep) {
        /* Open-air coverage: a failing COARSEST brick is kept when requested
         * (no descend -- there is nothing finer to resolve in empty space). */
        if (level == 0 && c->cfg->fill_empty) {
            if (emit) brick_emit(c, min, size, level); else c->count++;
            return 1;
        }
        return 0;   /* uncovered air: an ancestor must provide probes. */
    }

    /* Deep-buried cull: geometry is near (descend continues so thick-wall
     * FACES are found), but this brick's own probes would sit inside solid. */
    int buried = c->cfg->buried_frac > 0.0f &&
                 sd < -c->cfg->buried_frac * (size / 3.0f);

    int child_covered = 1;
    if (level + 1 < c->cfg->levels) {
        float child = size / 3.0f;
        for (int k = 0; k < 3; ++k)
            for (int j = 0; j < 3; ++j)
                for (int i = 0; i < 3; ++i) {
                    float cmin[3] = { min[0] + child * (float)i,
                                      min[1] + child * (float)j,
                                      min[2] + child * (float)k };
                    if (!brick_descend(c, cmin, child, level + 1, emit))
                        child_covered = 0;
                }
    } else {
        child_covered = 0;   /* leaf: nothing finer covers it. */
    }

    if (buried) return 1;    /* solid volume: covered by definition, no emit. */
    if (child_covered) return 1;   /* fully refined: suppress this brick. */
    if (emit) brick_emit(c, min, size, level); else c->count++;
    return 1;
}

/* Walk every coarse cell of the AABB cover in fixed z,y,x order. */
static void brick_walk(brick_ctx_t *c, int emit)
{
    const probe_brick_config_t *cfg = c->cfg;
    int n[3];
    for (int a = 0; a < 3; ++a) {
        float span = cfg->aabb_max[a] - cfg->aabb_min[a];
        n[a] = (int)ceilf(span / cfg->coarse_brick);
        if (n[a] < 1) n[a] = 1;
    }
    for (int k = 0; k < n[2]; ++k)
        for (int j = 0; j < n[1]; ++j)
            for (int i = 0; i < n[0]; ++i) {
                float min[3] = { cfg->aabb_min[0] + cfg->coarse_brick * (float)i,
                                 cfg->aabb_min[1] + cfg->coarse_brick * (float)j,
                                 cfg->aabb_min[2] + cfg->coarse_brick * (float)k };
                brick_descend(c, min, cfg->coarse_brick, 0, emit);
            }
}

bool probe_brick_place(const probe_brick_config_t *cfg, struct arena *arena,
                       probe_set_t *out_set, probe_brick_t **out_bricks,
                       uint32_t *out_n_bricks)
{
    if (cfg == NULL || arena == NULL || out_set == NULL || out_bricks == NULL ||
        out_n_bricks == NULL || cfg->sdf == NULL || cfg->coarse_brick <= 0.0f ||
        cfg->levels < 1 || cfg->levels > PROBE_BRICK_MAX_LEVELS)
        return false;

    brick_ctx_t c;
    memset(&c, 0, sizeof c);
    c.cfg = cfg;
    c.half_diag_scale = 0.8660254f; /* sqrt(3)/2 */
    /* Finest probe spacing is coarse/3^levels; quantize at half of it so exact
     * lattice coincidences merge and everything else stays distinct. */
    float finest = cfg->coarse_brick;
    for (int l = 0; l < cfg->levels; ++l) finest /= 3.0f;
    c.quant = finest * 0.5f;

    /* Pass 1: exact kept-brick count (probes are bounded by 64 per brick). */
    brick_walk(&c, 0);
    memset(out_set, 0, sizeof *out_set);
    *out_bricks = NULL;
    *out_n_bricks = 0;
    if (c.count == 0) return true;   /* an empty region is a valid result. */

    uint32_t max_probes = c.count * 64u;
    uint32_t hash_cap = 1;
    while (hash_cap < max_probes * 2u) hash_cap <<= 1;

    arena_t *ar = (arena_t *)arena;
    c.bricks = arena_alloc(ar, 16u, (size_t)c.count * sizeof(probe_brick_t));
    c.positions = arena_alloc(ar, 16u, (size_t)max_probes * 3u * sizeof(float));
    c.hash.keys = arena_alloc(ar, 16u, (size_t)hash_cap * sizeof(uint64_t));
    c.hash.values = arena_alloc(ar, 16u, (size_t)hash_cap * sizeof(uint32_t));
    if (c.bricks == NULL || c.positions == NULL || c.hash.keys == NULL ||
        c.hash.values == NULL)
        return false;
    memset(c.hash.keys, 0xff, (size_t)hash_cap * sizeof(uint64_t)); /* EMPTY */
    c.hash.mask = hash_cap - 1u;

    /* Pass 2: identical traversal, emitting bricks + deduplicated probes. */
    brick_walk(&c, 1);

    out_set->count = c.n_probes;
    out_set->positions = c.positions;
    *out_bricks = c.bricks;
    *out_n_bricks = c.n_bricks;
    return true;
}
