---
id: rust-rpg-dhr
status: closed
deps: []
links: []
created: 2026-02-01T08:29:18.777264508-08:00
type: task
priority: 2
---
# p007 core udp comms module

Implement a core UDP comms module (POSIX sockets) in src/net/udp/ with a small public API in include/ferrum/net/ for: create socket, bind, sendto, recvfrom, and address parsing/formatting as needed. This module is a prerequisite for the p007 client/server integration trajectory test.

## Notes

TDD: write UDP unit tests first (RED), then implement (GREEN). Keep headers <=2 public types and each .c <=4 non-static functions.


