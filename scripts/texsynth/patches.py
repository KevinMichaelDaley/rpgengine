"""Random region sampling from an exemplar image."""

import numpy as np


def sample_region(exemplar, height, width, rng):
    """Return a random (height, width[, C]) block copied from *exemplar*.

    The block is fully inside the exemplar. *rng* is a numpy Generator, so
    passing an equally-seeded generator yields the same region (deterministic).
    """
    eh, ew = exemplar.shape[:2]
    if height > eh or width > ew:
        raise ValueError("region larger than exemplar")
    y = int(rng.integers(0, eh - height + 1))
    x = int(rng.integers(0, ew - width + 1))
    return np.array(exemplar[y:y + height, x:x + width])


def random_patch(exemplar, size, rng):
    """Return a random square block of side *size* from *exemplar*."""
    return sample_region(exemplar, size, size, rng)
