---
id: rpg-5p3e
status: open
deps: [rpg-7klm, rpg-vmlk]
links: []
created: 2026-03-01T09:58:49Z
type: task
priority: 2
assignee: KMD
parent: rpg-9wui
tags: [aegis, vm, validation]
---
# Aegis validation engine

Implement the validation engine that filters update sets against rules per ref/aegis_bytecode_spec.md §7.3, §7.4, §8.

Implement:
- aegis_validate(rules, updates, world): iterate updates, for each flagged attribute key run matching rules, apply actions
- Check implementations:
  - distance_check: compare old position (from world snapshot) to new position; reject if delta > max_speed × dt × multiplier
  - range_check: clamp or reject if value exceeds configured max
  - rate_limit: track update frequency per entity per key; reject if rate > max_per_second
- Validation hints: HINT_MOVEMENT, HINT_AUTHORITY, HINT_PREDICTION influence rule behavior (e.g., movement hints increase distance tolerance)
- aegis_validation_result_t: per-update accept/reject status, aggregate flags

Files:
- src/aegis/aegis_validate.c (main validation loop)
- src/aegis/aegis_validate_checks.c (distance_check, range_check, rate_limit implementations)
- tests/aegis/aegis_validate_tests.c

Acceptance criteria:
- [ ] Updates to non-flagged attributes pass through unchanged
- [ ] distance_check rejects teleport-distance position changes
- [ ] distance_check allows normal-speed movement
- [ ] range_check clamps values exceeding max
- [ ] rate_limit rejects updates exceeding per-second threshold
- [ ] Validation hints influence check behavior (e.g., HINT_MOVEMENT increases tolerance)
- [ ] Rejected updates are dropped from the applied set
- [ ] Flagged scripts are recorded for review
- [ ] Tests: pass-through for unflagged, reject teleport, allow movement, clamp range, rate limit trigger, hint influence

## Acceptance Criteria

Validation engine correctly filters updates against rules, hints influence checks

