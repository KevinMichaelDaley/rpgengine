---
id: rpg-dquq
status: open
deps: []
links: []
created: 2026-07-04T20:40:15Z
type: task
priority: 0
assignee: KMD
parent: rpg-fizd
tags: [procgen, nitrogen, ipc, pipe]
---
# procgen-6b: Action pipe protocol (C side)

## Design

Implement nitrogen_ipc.c/h: a Unix pipe (or named pipe) for receiving actions from the Python NitroGen process. Protocol: newline-delimited JSON objects. Format: {"lx":float,"ly":float,"rx":float,"ry":float,"a":int,"b":int,"x":int,"y":int,"lb":int,"rb":int,"start":int,"select":int}. nitrogen_ipc_read_action() blocks with timeout, returns parsed action struct. Write RED test with mock JSON producer.

## Acceptance Criteria

- Named pipe created for action communication\n- JSON actions parsed correctly\n- All joystick axes (lx,ly,rx,ry) in [-1,1]\n- All button states (0/1) parsed\n- Timeout on no action received\n- Malformed JSON handled gracefully\n- Pipe cleanup on exit

