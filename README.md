# mini-chat-cpp

A single-threaded, event-driven multi-room chat server built with `epoll`.
Deliberately uses I/O multiplexing instead of thread-per-client (see
mini-redis-cpp for that model) to demonstrate both approaches.

## Features (current)
- epoll-based event loop handling many concurrent clients on one thread
- Multi-room support (`/join <room>`)
- Nicknames (`/nick <name>`)
- `/list` to see who's in your current room
- Broadcast messaging within a room

## Roadmap (see TODOs in code)
- [ ] Per-client rate limiting to prevent flooding (day 2)
- [ ] Persist + replay recent chat history per room (day 3)
- [ ] Private messaging: `/msg <user> <text>` (day 4)
- [ ] Basic token auth before allowing `/nick` (day 5)
- [ ] Unit tests for command parsing logic (day 5)

## Build

```bash
mkdir build && cd build
cmake ..
make
```

## Run

```bash
./mini_chat 7070
```

## Try it
Open multiple terminals:

```bash
nc localhost 7070
/nick alice
/join general
hello everyone!
```

## Why this project
Shows understanding of:
- I/O multiplexing (`epoll`) as an alternative to thread-per-connection
- Non-blocking sockets
- Designing stateful protocols (rooms, nicknames) over raw TCP
- A different concurrency model than mini-redis-cpp -- good talking point
  in interviews about tradeoffs (threads vs event loop)
