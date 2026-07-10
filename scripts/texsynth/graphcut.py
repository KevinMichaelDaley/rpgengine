"""Optimal 2-D seam between two overlapping patches via graph min-cut.

Kwatra et al. (2003): label every overlap pixel as belonging to patch A or B so
that the seam runs where the two patches agree. The cut cost of separating
adjacent pixels s, t is the matching cost M(s,t) = ||A(s)-B(s)|| + ||A(t)-B(t)||;
minimising total cut cost is a min-cut/max-flow problem, solved here with
``scipy.sparse.csgraph.maximum_flow`` (offline tooling, so SciPy is available).

Unlike the 1-D dynamic-programming boundary cut, this handles overlaps on more
than one side at once (the picture-frame border-vs-interior composite used to
build Wang tiles).
"""

import numpy as np
from scipy.sparse import csr_matrix
from scipy.sparse.csgraph import maximum_flow

_SCALE = 1000        # quantise float seam costs to the integer caps scipy needs
_INF = 1 << 30       # hard capacity binding a pixel to a terminal


def _pixel_diff(a, b):
    """Per-pixel magnitude of A-B: (H,W) float (L2 over channels if present)."""
    d = a.astype(np.float64) - b.astype(np.float64)
    if d.ndim == 3:
        return np.sqrt((d * d).sum(axis=2))
    return np.abs(d)


def _reach_from_source(residual, source, n):
    """Nodes reachable from *source* over residual edges with capacity > 0."""
    residual = residual.tocsr()
    indptr, indices, data = residual.indptr, residual.indices, residual.data
    visited = np.zeros(n, dtype=bool)
    visited[source] = True
    stack = [source]
    while stack:
        u = stack.pop()
        for k in range(indptr[u], indptr[u + 1]):
            v = indices[k]
            if data[k] > 0 and not visited[v]:
                visited[v] = True
                stack.append(v)
    return visited


def graphcut_seam(a, b, force_a=None, force_b=None):
    """Label each overlap pixel A or B along a minimum-cost seam.

    Args:
        a, b: aligned patches, (H,W) or (H,W,C).
        force_a, force_b: optional bool (H,W) masks pinning pixels to A / B.
    Returns:
        bool (H,W) mask, True where patch B should be used (A elsewhere).
    """
    a = np.asarray(a)
    b = np.asarray(b)
    dq = (_pixel_diff(a, b) * _SCALE).astype(np.int64) + 1  # +1 keeps graph connected
    h, w = dq.shape
    n = h * w
    src, sink = n, n + 1

    rows, cols, data = [], [], []

    def add(u, v, cap):
        rows.append(u)
        cols.append(v)
        data.append(int(cap))

    # 4-neighbour seam edges (undirected -> symmetric directed pair).
    for r in range(h):
        for c in range(w):
            p = r * w + c
            if c + 1 < w:
                cap = int(dq[r, c] + dq[r, c + 1])
                add(p, p + 1, cap)
                add(p + 1, p, cap)
            if r + 1 < h:
                cap = int(dq[r, c] + dq[r + 1, c])
                add(p, p + w, cap)
                add(p + w, p, cap)

    fa = np.zeros((h, w), bool) if force_a is None else np.asarray(force_a, bool)
    fb = np.zeros((h, w), bool) if force_b is None else np.asarray(force_b, bool)
    for r in range(h):
        for c in range(w):
            p = r * w + c
            if fa[r, c]:
                add(src, p, _INF)
            if fb[r, c]:
                add(p, sink, _INF)

    capacity = csr_matrix((data, (rows, cols)), shape=(n + 2, n + 2))
    result = maximum_flow(capacity, src, sink)
    residual = capacity - result.flow                 # forward + reverse residuals
    reachable = _reach_from_source(residual, src, n + 2)
    # Source side is A; everything else (sink side) is B.
    return ~reachable[:n].reshape(h, w)
