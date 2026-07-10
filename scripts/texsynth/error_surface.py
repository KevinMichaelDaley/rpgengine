"""Sum-of-squared-differences overlap error, the cost the boundary cut minimises."""

import numpy as np


def ssd(a, b):
    """Per-pixel squared error between two aligned regions.

    Args:
        a, b: arrays of identical shape, either (H, W) grayscale or (H, W, C).
    Returns:
        (H, W) float64 error surface; for multi-channel input the squared
        differences are summed across channels.
    """
    d = a.astype(np.float64) - b.astype(np.float64)
    d *= d
    if d.ndim == 3:
        return d.sum(axis=2)
    return d
