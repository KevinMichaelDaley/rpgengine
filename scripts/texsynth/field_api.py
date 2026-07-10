"""High-level texture-field synthesis: exemplar in, large aperiodic field out."""

from .patch_synth import synth_patchwork


def synth_field(exemplar, width, height, patch=192, overlap=None, seed=0):
    """Synthesise a (height, width[, C]) seamless, aperiodic field from *exemplar*.

    Places overlapping exemplar patches merged with irregular graphcut seams, so
    the field has no square-grid structure. Deterministic in *seed*.
    """
    return synth_patchwork(exemplar, width, height,
                           patch=patch, overlap=overlap, seed=seed)
