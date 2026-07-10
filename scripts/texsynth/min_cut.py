"""Efros-Freeman minimum-error boundary cut via dynamic programming.

Given an overlap error surface, find the least-cost 8-connected path from one
side to the other. Used to stitch overlapping patches along a seam that follows
the pixels where the two patches already agree.
"""

import numpy as np


def min_vertical_cut(error):
    """Least-cost top-to-bottom path through *error* (H, W).

    Returns an int array of length H giving the seam column in each row. The
    path is 8-connected: consecutive rows differ by at most one column.
    """
    error = np.asarray(error, dtype=np.float64)
    h, w = error.shape
    cost = error.copy()
    back = np.zeros((h, w), dtype=np.int64)
    inf = np.inf
    for i in range(1, h):
        prev = cost[i - 1]
        left = np.empty(w)
        left[0] = inf
        left[1:] = prev[:-1]
        right = np.empty(w)
        right[-1] = inf
        right[:-1] = prev[1:]
        stack = np.stack([left, prev, right])       # 0=left, 1=up, 2=right
        choice = np.argmin(stack, axis=0)
        cols = np.arange(w)
        back[i] = cols + (choice - 1)               # source column in row i-1
        cost[i] += np.min(stack, axis=0)
    seam = np.zeros(h, dtype=np.int64)
    seam[h - 1] = int(np.argmin(cost[h - 1]))
    for i in range(h - 1, 0, -1):
        seam[i - 1] = back[i, seam[i]]
    return seam


def min_horizontal_cut(error):
    """Least-cost left-to-right path; returns the seam row per column."""
    return min_vertical_cut(np.asarray(error, dtype=np.float64).T)


def cut_mask_from_cols(cols, width):
    """Boolean (H, width) mask: True strictly right of the seam column per row.

    True selects the NEW (right/second) patch; False keeps the existing (left)
    patch. The seam column itself stays with the existing patch.
    """
    cols = np.asarray(cols, dtype=np.int64)
    grid = np.arange(width)[None, :]
    return grid > cols[:, None]
