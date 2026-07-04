---
id: rpg-1ssx
status: closed
deps: [rpg-8ijf, rpg-vgqt, rpg-tljj, rpg-zx6t]
links: []
created: 2026-07-04T20:39:26Z
type: task
priority: 0
assignee: KMD
parent: rpg-uzd4
tags: [procgen, architect, vlm, tdd, integration]
---
# procgen-4e: P4 integration test

rpg-uzd4

## Design

End-to-end architect test: provide a mock VLM backend that returns pre-canned responses (1 valid, 3 invalid then 1 valid) to test the full pipeline. Verify: valid response → parse → rasterize → layout. Verify: invalid responses → reprompt → eventual success. Verify: all-invalid → exhaustion → error. Test budget exceeded and timeout handling.

## Acceptance Criteria

- Valid response pipeline works end-to-end\n- Reprompting handles parse errors correctly\n- Exhaustion after max_retries handled\n- Budget exceeded returns correct error\n- Timeout handled gracefully\n- Token/cost stats accurate\n- RED-GREEN-REFACTOR complete

