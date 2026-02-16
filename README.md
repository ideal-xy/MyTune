# MyTune — Event-Driven CLI Music Player

MyTune is a high-performance command-line music player built on an event-driven architecture with system-level process control.  
It avoids busy-waiting entirely and relies on OS kernel event notification mechanisms to achieve low CPU usage and responsive interaction.

The project focuses on practicing low-level systems programming concepts such as process management, signal handling, and asynchronous I/O multiplexing.

---

## Features

- Event-driven playback control (no polling loops, no busy waiting)
- Real-time progress bar rendering
- Pause, resume, skip, and quit via keyboard input
- Low CPU and memory overhead
- Pure system-call based implementation (no external playback libraries)

---

## Architecture Overview

MyTune follows a parent-child process model:

- The child process executes the system audio backend (`afplay`)
- The parent process acts as an event dispatcher

Core components:

- Process control via `fork`, `exec`, `waitpid`, and POSIX signals
- Kernel event notification using `kqueue` (macOS) / `poll` (portable fallback)
- Non-blocking terminal input handling
- Time-driven rendering through kernel timers

This design ensures:

- No busy loops
- Precise timing control
- Immediate response to user commands

---

## Controls

| Key | Action |
|-----|-------|
| `s` | Pause playback |
| `c` | Continue playback |
| `n` | Skip to next track |
| `q` | Quit player |

---

## Performance Characteristics

- Parent process CPU usage typically below 0.1%
- Playback handled entirely by child process
- No blocking sleeps or spin loops
- Scales efficiently for long playback sessions

---

## Build & Run

Example:

```bash
g++ -std=c++17 -O3 main.cpp -o Mytune
./Mytune <music_directory> ****
```