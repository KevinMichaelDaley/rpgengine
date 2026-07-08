---
id: rpg-uuft
status: closed
deps: [rpg-xud8]
links: []
created: 2026-07-05T22:56:06Z
type: task
priority: 1
assignee: KMD
parent: rpg-ct3l
tags: [srd, discrete]
---
# srd-discrete-02: compatibility graph and greedy max-cover

Implement the compatibility graph and greedy max-cover. Two candidates are compatible iff the sets of boxes within locality_radius of their respective selections do not overlap. Build NxN bool matrix. Greedy max-cover: sort positive-delta_L candidates by delta_L descending, greedily include each compatible with all already-selected.

## Design

Affected boxes for candidate c: all boxes j where euclidean_dist(layout.boxes[j].centre, selection_centre(c)) < c.rule.locality_radius. Compatibility: affected(c1) ∩ affected(c2) = empty. Matrix is NxN where N=number of positive candidates (≤K). Greedy O(N^2) is fine for N<=512. Selected candidates applied in order of selection.

## Acceptance Criteria

Two candidates operating on the same box are marked incompatible; two candidates operating on distant boxes are compatible; greedy selects at least one candidate when any positive delta_L exists; all selected candidates are pairwise compatible (verified by post-selection check); total delta_L of selected set >= any single candidate's delta_L

