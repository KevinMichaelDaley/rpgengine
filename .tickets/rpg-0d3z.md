---
id: rpg-0d3z
status: closed
deps: []
links: []
created: 2026-07-04T20:40:15Z
type: task
priority: 0
assignee: KMD
parent: rpg-fizd
tags: [procgen, nitrogen, ipc, shm]
---
# procgen-6a: Shared memory frame ring buffer (C side)

## Design

Implement nitrogen_shm.c/h: a lock-free single-producer single-consumer (SPSC) ring buffer in shared memory. Producer (renderer thread): nitrogen_shm_write_frame(rgb_data, width, height). Consumer (Python): reads via mmap. Header struct with magic number, frame dimensions, sequence number, write index. Ring of frame slots with sequence numbers for consumer to detect new frames. Handle buffer full (producer drops old frames). Write RED test with mock consumer.

## Acceptance Criteria

- SPSC ring buffer with N slots\n- Producer writes without blocking consumer\n- Consumer reads latest frame\n- Sequence numbers for new-frame detection\n- Buffer full: producer overwrites oldest\n- Shared memory created with correct permissions\n- mmap-able from Python process

