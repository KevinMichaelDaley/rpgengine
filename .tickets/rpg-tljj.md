---
id: rpg-tljj
status: closed
deps: [rpg-vgqt]
links: []
created: 2026-07-04T20:39:26Z
type: task
priority: 0
assignee: KMD
parent: rpg-uzd4
tags: [procgen, architect, vlm, reprompt]
---
# procgen-4c: Reprompting loop

## Design

Implement the reprompting loop: after VLM response, attempt to parse via grammar's tokenize(). If parse fails, append error message to conversation context: 'Your previous output failed to parse: [error]. Please generate ONLY valid grammar tokens.' Resend to VLM. Repeat up to max_retries (default 3). Track attempt count. On success, return token string. On exhaustion, return error with last parse failure details.

## Acceptance Criteria

- Parse failure triggers retry with error context\n- Retry count limited to max_retries\n- Each retry tracks attempt number\n- Context accumulates parse errors\n- Success after retry returns valid string\n- Exhaustion returns descriptive error\n- No infinite loops

