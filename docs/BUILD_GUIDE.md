# Building a Redis Clone in C++: a learning walkthrough

This guide takes you from the current `PING` server to a small, dependable
Redis-compatible database. The goal is not to reproduce every Redis feature.
It is to make deliberate engineering choices you can explain in a portfolio or
interview: protocol parsing, concurrent I/O, data modelling, expiration,
persistence, and testing.

## What to build

Aim for this final scope:

- RESP2 protocol with pipelining
- `PING`, `ECHO`, `SET`, `GET`, `DEL`, `EXISTS`, `INCR`, `TYPE`
- Key expiry (`EX` and `PX`, plus `TTL`/`PTTL`)
- Strings, lists, hashes, and sets
- Multiple simultaneous clients
- Append-only persistence and recovery
- Basic observability (`INFO`, command counters, logs)

The result is intentionally a single-process, in-memory server. Replication,
clustering, Lua scripting, transactions, and Redis Streams are excellent
stretch goals, but should not block the core project.

## Guiding principles

1. **Keep each milestone runnable.** Add one observable feature at a time and
   retain tests for it.
2. **Parse bytes, not messages.** TCP is a stream: one `recv` can contain part
   of a command, one command, or several commands.
3. **Separate protocol, command, and storage code.** This is the biggest
   difference between a demo and a maintainable server.
4. **Use the Redis CLI as an integration client.** It demonstrates protocol
   compatibility rather than only testing internal C++ functions.
5. **Document decisions.** A short architecture note and benchmark report make
   the repository much stronger as a showcase.

## Suggested layout

Refactor the current single source file early. Keep platform-specific socket
code behind a small interface so the database code stays portable.

```text
src/
  main.cpp                 # configuration, signal handling, startup
  server.{h,cpp}           # listener and client lifecycle
  net/socket.{h,cpp}       # Socket wrapper; Windows/POSIX implementation
  protocol/resp.{h,cpp}    # incremental RESP decoder and encoder
  commands/dispatcher.{h,cpp}
  commands/strings.cpp
  commands/collections.cpp
  storage/database.{h,cpp} # keyspace, values, TTL metadata
  storage/value.h          # variant of supported Redis-like types
  persistence/aof.{h,cpp}
  util/clock.h
tests/
  resp_test.cpp
  database_test.cpp
  integration/
```

Keep the build target named `redis`. Add a separate test executable through
CMake when the first unit tests arrive. Avoid making the command handlers call
socket APIs directly: handlers should receive parsed arguments and return a
protocol response.

---

## Step 0 — Establish a clean development loop

**Learn:** CMake targets, compiler warnings, debugging, and black-box testing.

1. Build with warnings enabled (`-Wall -Wextra -Wpedantic` for GCC/Clang, or
   `/W4` for MSVC). Treat new warnings as defects.
2. Add a `--port` command-line option, defaulting to `6379`; this allows tests
   to run without interfering with a local Redis instance.
3. Install or use `redis-cli` for end-to-end checks. At this stage:

   ```sh
   redis-cli -p 6379 PING
   # expected: PONG
   ```

4. Add a `tests/` target using a lightweight framework (Catch2 or doctest) or a
   tiny assertion harness. Test code should run without opening a TCP port
   unless it is specifically an integration test.

**Deliverable:** README build/run instructions and a short `docs/ARCHITECTURE.md`
describing the component boundaries above.

## Step 1 — Make the TCP server correct before making it clever

**Learn:** BSD sockets/Winsock, blocking I/O, resource ownership, and TCP
stream behavior.

Replace the current “read once, answer once, close” loop with a `ClientSession`:

```cpp
class ClientSession {
 public:
  explicit ClientSession(Socket socket);
  void run();

 private:
  Socket socket_;
  std::string input_buffer_;
};
```

`run()` repeatedly reads bytes, appends them to `input_buffer_`, extracts as
many complete requests as possible, and sends a response for each. Do **not**
assume a `recv` call lines up with a client `send` call. Use RAII to ensure a
socket is closed on every return path; a small movable `Socket` class is better
than raw `int`/`SOCKET` values scattered through the code.

Initially, accept clients serially and retain the blocking model. It makes
protocol bugs much easier to see. Set a receive limit such as 64 KiB or 1 MiB;
without it, an incomplete malicious request can grow the input buffer forever.

**Tests to add:** fragmented input, two commands in one write, a client that
disconnects mid-command, and an oversized request.

## Step 2 — Implement RESP2 incrementally

**Learn:** grammar-driven parsing, state machines, framing, error recovery, and
binary-safe data.

RESP2 represents commands as arrays of bulk strings. For example,
`SET color blue` is:

```text
*3\r\n$3\r\nSET\r\n$5\r\ncolor\r\n$4\r\nblue\r\n
```

Model protocol values explicitly:

```cpp
struct RespValue {
  enum class Type { SimpleString, Error, Integer, BulkString, Array, Null };
  Type type;
  std::string string;                 // can contain '\0' for bulk strings
  std::int64_t integer{};
  std::vector<RespValue> array;
};

enum class ParseStatus { Complete, Incomplete, Error };
```

Make the decoder return `Incomplete` without consuming an unfinished frame.
Only erase the consumed prefix from `input_buffer_` after a complete parse.
This single rule prevents most partial-read bugs. Validate length fields before
converting them and cap bulk-string/array sizes. Encode replies through matching
helpers: `simple("PONG")`, `error("ERR ...")`, `integer(1)`, and
`bulk(std::nullopt)`.

Begin by accepting only top-level arrays of bulk strings as commands. Respond
with Redis-style errors (`-ERR unknown command 'foo'\r\n`) rather than crashing
on malformed input.

**Tests to add:** every RESP data type, all split points of a request, multiple
requests in one buffer, negative lengths, invalid terminators, and embedded
NUL bytes in bulk strings.

## Step 3 — Build a command dispatcher and string keyspace

**Learn:** API design, normalisation, `std::variant`, `std::unordered_map`, and
clear error semantics.

Dispatch parsed arrays with a registry rather than a long `if` chain:

```cpp
using Command = std::function<RespValue(Database&, std::span<const std::string>)>;
std::unordered_map<std::string, Command> commands;
```

Uppercase only the command name; keep keys and values byte-for-byte intact.
For each handler, validate arity first, then validate options, then execute.
This ordering produces predictable errors and simple tests.

Start with strings:

| Command | Important behavior |
| --- | --- |
| `PING [message]` | `PONG` without an argument; bulk-string echo with one |
| `ECHO message` | returns a bulk string |
| `SET key value` | overwrites the key and clears its previous expiry |
| `GET key` | returns a null bulk string for a missing key |
| `DEL key ...` | returns the number actually removed |
| `EXISTS key ...` | counts existing keys |
| `TYPE key` | `none` for a missing key |

The database should own values, not command handlers:

```cpp
using Value = std::variant<StringValue, ListValue, HashValue, SetValue>;

struct Entry {
  Value value;
  std::optional<TimePoint> expires_at;
};

class Database {
  std::unordered_map<std::string, Entry> entries_;
};
```

**Portfolio checkpoint:** demonstrate `redis-cli` commands, then add a small
script that starts the server on a random port and runs the same checks in CI.

## Step 4 — Add expiration as a first-class concern

**Learn:** monotonic clocks, lazy versus active cleanup, time arithmetic, and
data-structure trade-offs.

Use `std::chrono::steady_clock` for expiry deadlines, not `system_clock`: wall
clock changes must not make keys suddenly live longer or shorter. Centralise
expiry checks in `Database::find_live(key)`. Every command that reads a key
must go through it; if it finds an expired entry, erase it and report a miss.

Implement `SET key value EX seconds`, `PX milliseconds`, `EXPIRE`, `TTL`, and
`PTTL`. Define exact edge cases in tests: a non-existent key, no expiry,
non-positive expiry, and a key expiring between two commands.

Start with lazy expiry. Then add an optional background reaper that samples or
walks expiry metadata periodically. A min-heap of `(deadline, key, version)`
is a nice advanced implementation: the version prevents an old heap record
from deleting a key whose TTL was renewed.

## Step 5 — Support more data types and enforce type safety

**Learn:** C++ variants, move semantics, container complexity, and abstraction
boundaries.

Add one type at a time, including its unit tests and wrong-type errors.

| Type | C++ representation | Commands | Concepts to explain |
| --- | --- | --- | --- |
| Integer string | `std::string` parsed by `from_chars` | `INCR`, `DECR` | overflow/error handling |
| List | `std::deque<std::string>` | `LPUSH`, `RPUSH`, `LPOP`, `LRANGE` | head/tail complexity, index normalization |
| Hash | `std::unordered_map<std::string, std::string>` | `HSET`, `HGET`, `HDEL` | nested storage, count replies |
| Set | `std::unordered_set<std::string>` | `SADD`, `SREM`, `SISMEMBER` | uniqueness and membership complexity |

Commands should call `get_as<T>(key)` (or a visitor over `Value`) and return
`WRONGTYPE Operation against a key holding the wrong kind of value` when the
stored type differs. Do not silently coerce types.

## Step 6 — Handle many clients

**Learn:** concurrency models, shared-state safety, race conditions, and
performance trade-offs.

Implement this in two iterations so you can compare them:

1. **Thread per connection.** Run each `ClientSession` in a `std::jthread` and
   guard the database with `std::shared_mutex` (`shared_lock` for reads,
   `unique_lock` for writes). It is easy to understand and ideal for a first
   version.
2. **Event loop or worker pool.** Move sockets to non-blocking mode and use
   `select`/`poll` (or platform equivalents) to service many connections with a
   small fixed number of threads. Keep the protocol parser per connection; only
   the database is shared.

Avoid holding a database mutex while doing socket I/O or append-only-file I/O.
Use a stress test with many clients incrementing one key; the expected final
value reveals lost updates immediately. `INCR` must read, validate, increment,
and write while holding one exclusive lock.

## Step 7 — Persist writes with an append-only file

**Learn:** write-ahead logging, crash recovery, durability/performance trade
offs, and deterministic replay.

Use an AOF file containing the same RESP command arrays clients send. When a
mutating command succeeds, append its canonical command before acknowledging
it (stronger durability) or batch/flush it periodically (better throughput).
State the chosen policy in your README.

At startup, parse and dispatch the AOF in a special recovery mode that skips
network responses and does not append commands again. Handle a truncated final
record gracefully: replay complete commands and warn about the remainder.
Use a mutex or a single writer queue so concurrent commands cannot interleave
bytes in the file.

**Tests to add:** write data, stop the server, restart it, and assert recovery;
then repeat with TTLs and a deliberately truncated final record.

## Step 8 — Make it operable and presentable

**Learn:** observability, configuration, benchmarking, reproducibility, and
technical communication.

Add an `INFO` command with version, uptime, connected client count, command
count, hit/miss count, and key count. Add structured, levelled logs to stderr;
never log binary values verbatim. Support settings through command-line flags:

```text
redis --port 6380 --data-dir ./data --appendonly yes --log-level info
```

Benchmark the final project with `redis-benchmark` or a small client of your
own. Report the machine, concurrency, commands, payload size, and throughput;
do not present a number without its conditions. Profile before optimising.

Finish with a short demo:

1. Run the server and show normal `redis-cli` use.
2. Send pipelined commands.
3. Show a key expiring.
4. Restart after writes and show recovered data.
5. Run the test suite and a concurrent increment stress test.

## Verification checklist

Before calling the project complete, verify all of the following:

- A fragmented request and a pipelined request both produce correct replies.
- Arbitrary binary bulk-string values round-trip unchanged.
- Invalid protocol input returns an error without taking down the server.
- Expired keys act missing for every command.
- Wrong-type commands return `WRONGTYPE` and preserve the original value.
- Multiple clients do not lose increments.
- A restart restores successfully acknowledged writes under the documented AOF
  durability mode.
- Unit, integration, and stress tests run from one documented command.

## Strong stretch goals

Choose one or two only after the checklist passes:

- `MULTI`/`EXEC` with queued command validation
- `SCAN` cursor iteration without blocking the server
- Pub/sub, including subscriber connection mode
- RDB-style point-in-time snapshots plus AOF rewrite
- An LFU/LRU eviction policy with a configurable memory limit
- RESP3 support and client tracking

Each is valuable because it forces a new systems-design conversation. A smaller
server with excellent tests, a clear architecture, and a polished demo will
showcase your skills more convincingly than an unfinished list of Redis
features.
