# Interview Questions, by Module

A section is added after every module. Each question has a short model answer
that you should be able to say in your own words, and a **Show it** line that
points at code or a test in this repo you can open during the interview.

`ROADMAP.md` section 9 has the project-wide questions. This file is the
module-level detail behind them.

- [Module 1: Build system and unit-test harness](#module-1-build-system-and-unit-test-harness)
- [Module 2: RAII `Socket` and the SIGPIPE fix](#module-2-raii-socket-and-the-sigpipe-fix)

---

## Module 1: Build system and unit-test harness

**30-second pitch:** "I split the server into a static library plus a thin
`main`, so unit tests link the same code the server runs without opening a port.
Warnings and sanitizer flags live in one CMake target. I also check that tests
can fail, by breaking the code on purpose and watching them go red."

### Q1. Why is there a `redis_core` library and a separate `redis` executable?

- A test program cannot call `main()`. If all code lived in the executable, the
  only way to test it would be to start the server and talk to it over TCP.
- Everything except `main()` goes in a library. The server and `unit_tests` both
  link it, so the tests exercise the real code, not a copy.
- It also forces a clean boundary: `main` only wires things together.

**Show it:** `CMakeLists.txt` (`add_library(redis_core ...)`, `add_executable(redis src/main.cpp)`).

### Q2. What is the difference between `PUBLIC`, `PRIVATE` and `INTERFACE` in `target_link_libraries`?

- `PRIVATE`: used to build this target only; consumers do not inherit it.
- `PUBLIC`: used to build this target and also inherited by anything that links it.
- `INTERFACE`: not used by this target itself, only passed on to consumers.
- Here `redis_options` is an `INTERFACE` library with no code, only flags.
  `redis_core` links it `PUBLIC`, so `redis` and `unit_tests` get the same
  warnings and sanitizer flags automatically.

### Q3. What do AddressSanitizer, UBSan and ThreadSanitizer each catch? Can you combine them?

- **ASan:** heap/stack buffer overflows, use-after-free, double free, leaks.
- **UBSan:** undefined behaviour such as signed overflow, bad shifts, misaligned access.
- **TSan:** data races (two threads touch the same memory, at least one writes, no ordering).
- ASan + UBSan combine fine (`-DREDIS_SANITIZE=address,undefined`).
- TSan cannot be combined with ASan: they both reserve and manage shadow memory
  in incompatible ways. Run them as separate builds.
- Sanitizers need the flag at both compile and link time, which is why
  `redis_options` sets both.

**Follow-up:** "Why not run sanitizers all the time?" They slow the program roughly 2x to 10x and use more memory, so they run in tests and CI, not in production or benchmarks.

### Q4. How do you know your tests can actually fail?

- A test that has never been red proves nothing. After writing a test, break the
  code on purpose (a *negative control*) and confirm the test fails.
- In Module 1, changing the upper bound `65535` to `65534` made the boundary
  test fail. In Module 2, removing `MSG_NOSIGNAL` made the test process die.
- Phase 6 reuses this idea at scale: a deliberately broken `INCR` must make the
  stress test fail, or the stress test is not sensitive to races.

### Q5. Why does `parse_port` return `std::expected` instead of printing an error, throwing, or returning `optional`?

- Printing inside the function makes it hard to test and hard to reuse.
- `optional` says "it failed" but not why.
- Exceptions are fine in general, but for expected, user-caused failures such as
  a bad command-line value, returning the error as a value keeps control flow
  explicit and cheap.
- `std::expected<T, E>` (C++23) carries either the value or the error message,
  and the caller decides what to do. Here `main` prints it; the tests inspect it.

### Q6. Why `std::from_chars` and not `std::stoi` or `atoi`?

- `stoi` throws on failure and silently accepts trailing garbage (`"12abc"` gives 12) and leading whitespace.
- `atoi` returns 0 on failure, which is indistinguishable from a real `0`, and has undefined behaviour on overflow.
- `from_chars` never throws, never skips whitespace, never allocates, and tells you
  both whether it failed and where it stopped. Comparing the stop position to
  the end of the input is what rejects `12abc`.
- It returns an error code on overflow instead of wrapping silently.

**Show it:** `src/config.cpp` and the "rejects values that are not a whole in-range number" test.

### Q7. Why does the project use FetchContent for doctest rather than copying the header into the repo?

- The repo stays small and the dependency version is pinned (`GIT_TAG v2.4.12`), so builds are reproducible.
- Trade-off: the first configure needs network access. A vendored copy would work offline but adds a large third-party file to the repo.
- `REDIS_BUILD_TESTS=OFF` builds the server without downloading anything.

### Q8. Why do tests and manual runs use a port other than 6379?

- A real Redis may already be listening on 6379. Using another port avoids a collision.
- Later integration tests use random ports so several test runs can happen in parallel.

---

## Module 2: RAII `Socket` and the SIGPIPE fix

**30-second pitch:** "Sockets are owned by a move-only `Socket` class whose
destructor closes the handle, so no code path can leak a descriptor. It also
hides the Windows/Linux differences in `send` and `recv`. I fixed a Linux crash
where writing to a client that had already disconnected raised SIGPIPE and
killed the server, and I have a test that fails if the fix is removed."

### Q1. What is RAII and why use it for sockets?

- **R**esource **A**cquisition **I**s **I**nitialization: a resource is acquired
  in a constructor and released in the destructor. Destructors run when an object
  goes out of scope, including on early `return` and when an exception unwinds
  the stack.
- The old code called `close_socket` by hand on every error path. Each new
  `return` or `break` was a chance to forget it and leak a descriptor. A leaked
  descriptor eventually hits the per-process limit and the server can no longer
  accept clients.
- With `Socket`, the code that creates the connection cannot forget to close it.

**Show it:** `handle_client` in `src/utils.cpp` has several `break` paths and no `close`; `main.cpp` has no `close_socket` calls at all.

### Q2. Why is `Socket` move-only? What goes wrong if it is copyable?

- A copy would give two objects the same handle. Both destructors would close it
  (a double close).
- The second close is worse than a no-op. File descriptors are reused: the OS
  hands out the lowest free number. By the time the second `close` runs, that
  number may belong to a completely different connection or file that another
  thread just opened, and you would close someone else's resource. In a
  multithreaded server this is a real, hard-to-reproduce bug.
- So the copy operations are `= delete`d, and ownership is transferred with a move.
  After a move, the source holds `kInvalidSocket` and closes nothing.

### Q3. Walk me through the move constructor and move assignment.

- Constructor: `handle_(std::exchange(other.handle_, kInvalidSocket))` takes the
  handle and leaves the source empty in one step.
- Assignment must first **release what it already owns** (`reset()`), then take
  the other's handle. Forgetting the first step leaks the old socket.
  A negative control confirmed this: without `reset()` the test "move assignment
  closes the socket it replaces" fails.
- The `this != &other` check makes self-assignment safe. Without it,
  `reset()` would close the handle and then the exchange would copy back an
  already-closed one.

### Q4. Why are the move operations `noexcept`?

- Moving a handle cannot fail, so saying so is accurate.
- It matters to the standard library. `std::vector` growth uses
  `std::move_if_noexcept`: it only moves elements if the move cannot throw,
  otherwise it falls back to copying to keep its strong exception guarantee. For
  a move-only type, a throwing move would give up that guarantee.
- The `static_assert`s in `tests/unit/socket_test.cpp` lock this in.

### Q5. Is this the Rule of Three, Five or Zero?

- The class defines a destructor, so it follows the **Rule of Five**: if you
  write one of destructor / copy ctor / copy assign / move ctor / move assign,
  decide all five. Here copy is deleted and move is written by hand.
- The **Rule of Zero** is the ideal when possible: hold members that already
  manage themselves (`std::string`, `std::unique_ptr`) and write none of the
  five. A raw OS handle is not self-managing, which is why `Socket` exists.
  (A `std::unique_ptr` with a custom deleter could do the same job.)

### Q6. What is SIGPIPE and why did it crash the server?

- On Linux, writing to a socket whose peer has already closed it makes the
  kernel send your process the `SIGPIPE` signal. Its default action is to
  **terminate the process**. One client disconnecting at the wrong moment would
  take down every other client.
- Windows has no SIGPIPE; `send` just fails with an error.

### Q7. What are the ways to fix SIGPIPE, and why `MSG_NOSIGNAL`?

| Option | Scope | Notes |
| --- | --- | --- |
| `send(..., MSG_NOSIGNAL)` | one call | Linux/BSD. Local to our code; `send` returns -1 with `EPIPE`. |
| `signal(SIGPIPE, SIG_IGN)` | whole process | Simple, but changes global state that other libraries may rely on. Also covers writes to pipes. |
| `SO_NOSIGPIPE` socket option | one socket | macOS/BSD; `MSG_NOSIGNAL` does not exist there. |

- I chose `MSG_NOSIGNAL` because the fix lives next to the `send` call and does
  not touch process-wide state.
- Limit: it only covers `send`. If you later write to a pipe or file, SIGPIPE can still occur.
- On a platform without `MSG_NOSIGNAL` the code falls back to flag `0`; a
  macOS port would need `SO_NOSIGPIPE`. I did not claim macOS support.

### Q8. The first write to a closed connection often succeeds. Why? How does your test handle that?

- `send` only copies data into the kernel's send buffer and returns. The kernel
  then transmits it; the peer (whose socket is gone) answers with a TCP RST.
  Only **after** the RST arrives does the next `send` fail with `EPIPE`.
- So one write is not enough to see the failure. The test writes in a loop
  (with a short sleep, up to 100 tries) until `write_all` returns `false`.
- Real-world meaning: you cannot detect a dead client from the first write.
  Detection comes from a later write error or from `recv` returning 0.

**Show it:** the last test in `tests/unit/socket_test.cpp`. With `MSG_NOSIGNAL` removed, the test process exits with code 141 (128 + signal 13).

### Q9. What do the return values of `recv` mean?

- `> 0`: that many bytes arrived. It can be fewer than you asked for, and unrelated to how the peer split its `send` calls (TCP is a byte stream).
- `0`: the peer performed an orderly shutdown. It will send nothing more.
- `< 0`: an error, with the reason in `errno` (`WSAGetLastError()` on Windows).
- `EINTR` is not a real error: a signal interrupted the call before any data
  moved, so `read_some` just retries.

### Q10. Why does `write_all` loop? Doesn't a blocking `send` write everything?

- Not guaranteed. `send` may accept only part of the data, for example if a signal
  interrupts it after a partial transfer, or if a send timeout is set. On
  non-blocking sockets (a later event-loop design) partial writes are routine.
- `write_all` advances through the buffer until it is empty and returns `false`
  on a real error. It treats a return of 0 as a failure too, so it can never spin forever.

### Q11. Why hide `recv`/`send` inside `Socket`?

- The raw APIs differ between platforms: `recv` returns `int` on Windows and
  `ssize_t` on Linux, the length parameter is `int` versus `size_t`, closing is
  `closesocket` versus `close`, and there is no SIGPIPE on Windows.
- `Socket` is the one place with `#ifdef _WIN32`. The rest of the server sees one interface, and `clamp_length` stops a huge length wrapping into a negative `int` on Windows.

### Q12. Why must `NetworkRuntime` be declared before any `Socket`?

- Local objects are destroyed in reverse order of construction. `NetworkRuntime`'s
  destructor calls `WSACleanup()` on Windows. If it were destroyed before a
  `Socket`, that socket's `closesocket` would run after Winsock was shut down and fail.
- Declaring it first means it is destroyed last.

### Q13. What does `SO_REUSEADDR` do, and why is it set on the listening socket?

- After a server exits, its old connections can sit in `TIME_WAIT` for a while.
  Without the option, `bind` to the same port fails with "Address already in use"
  when you restart quickly.
- On Linux, it does **not** allow two live listeners on one port (that is
  `SO_REUSEPORT`). On Windows the option behaves differently, which is a known portability trap.

### Q14. How did you test networking code without a real server?

- The test creates a listening socket on `127.0.0.1` with port `0` (the OS picks
  a free port), connects to it, and accepts. That gives two real connected
  `Socket`s in one process, so no fixed port is used and nothing external is needed.
- Test sockets get a 2 second receive timeout. Without it, a bug that leaves a
  connection open makes a blocking read hang the whole suite instead of failing.
  I found this when one of my own negative controls hung.
- I also checked the server itself: 1,000 normal connections plus 200 abrupt
  resets left the descriptor count unchanged (4 before, 4 after) and the server
  still answered `PING`.
