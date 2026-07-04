---
id: rpg-s5wr
status: open
deps: [rpg-wksd, rpg-hbng, rpg-ry2l, rpg-zl24, rpg-n8ay]
links: []
created: 2026-07-04T20:40:15Z
type: task
priority: 0
assignee: KMD
parent: rpg-oxnh
tags: [procgen, critic, runtime, tdd, integration]
---
# procgen-7f: P7 integration test

rpg-oxnh

## Design

Full critic integration test: create a simple 2-room blockout dungeon (token string → layout → physics world). Register hooks via P5. Run 3 playthroughs with mock NitroGen agent (random actions from P6d). Verify: hooks fire, events collected, per-playthrough stats computed, summary report generated. Test timeout path. Test all-markers-reached success path.

## Acceptance Criteria

- Full critic pipeline: layout → playthroughs → stats → report\n- All hook events collected\n- Per-playthrough stats accurate\n- Summary statistics correct\n- Report generated in text format\n- Timeout and success termination work\n- RED-GREEN-REFACTOR complete

