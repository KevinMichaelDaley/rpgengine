---
id: rpg-pf3x
status: closed
deps: []
links: []
created: 2026-02-26T04:26:35Z
type: task
priority: 1
assignee: KMD
parent: rpg-wxom
tags: [editor, foundation, controller]
---
# Controller TUI framework (ctrl_tui.c)

Implement the controller's terminal UI framework: raw termios, ANSI rendering, input handling, poll-based event loop.

READ FIRST: ref/editor_design.md §9 for TUI implementation details (terminal setup, event loop, rendering, input state machine), ref/editor_ux.md §2 for layout, §3 for command-line interface, §3.1 for input modes.

The controller is a standalone process with a curses-style TUI. It connects to the server (edit protocol TCP) and client (state socket TCP).

Requirements:
- Raw termios mode (not ncurses — fewer dependencies)
- Single-threaded poll() event loop: stdin + server_fd + client_fd
- ANSI escape sequence rendering (double-buffered: build in buffer, single write)
- Three regions: status bar (top), log area (middle, scrollable), command-line (bottom)
- Input state machine with 5 modes: Normal, Command, REPL, Grab, Context
- Vim-style numeric prefix accumulation in Normal mode
- Color scheme: inverse status bar, red errors, yellow warnings, cyan entity refs
- Window resize handling (SIGWINCH)

Files to create:
- include/ferrum/editor/ctrl_tui.h
- src/editor/controller/ctrl_tui.c (rendering)
- src/editor/controller/ctrl_input.c (input state machine)
- src/editor/controller/ctrl_main.c (event loop + main)
- src/editor/controller/ctrl_log.c (log area management)

Dependencies: none (standalone process)

