#include "socket.h"

#include <doctest/doctest.h>

#include <chrono>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>

#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/time.h>
#endif

static_assert(!std::is_copy_constructible_v<Socket>);
static_assert(!std::is_copy_assignable_v<Socket>);
// std::vector only moves elements on growth if the move cannot throw.
static_assert(std::is_nothrow_move_constructible_v<Socket>);
static_assert(std::is_nothrow_move_assignable_v<Socket>);

namespace {

struct ConnectedPair {
  Socket client;
  Socket server;
};

// If a Socket bug leaves a connection open, a blocking read would hang the
// whole test run. With a timeout the read fails (-1) and the test goes red.
void set_receive_timeout(const Socket& socket) {
#ifdef _WIN32
  const DWORD timeout = 2000;  // milliseconds
#else
  const timeval timeout{2, 0};
#endif
  REQUIRE(setsockopt(socket.get(), SOL_SOCKET, SO_RCVTIMEO,
                     reinterpret_cast<const char*>(&timeout),
                     sizeof(timeout)) == 0);
}

// A real TCP connection over loopback. Port 0 asks the OS for any free port,
// so the tests never clash with a running server.
ConnectedPair make_connected_pair() {
  static NetworkRuntime runtime;  // Winsock startup on Windows, no-op on Linux
  static const bool ready = runtime.initialize();
  REQUIRE(ready);

  Socket listener(socket(AF_INET, SOCK_STREAM, 0));
  REQUIRE(listener.valid());

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = 0;
  const SocketLength address_size = sizeof(address);

  REQUIRE(bind(listener.get(), reinterpret_cast<const sockaddr*>(&address),
               address_size) == 0);
  REQUIRE(listen(listener.get(), 1) == 0);

  SocketLength reported_size = address_size;
  REQUIRE(getsockname(listener.get(), reinterpret_cast<sockaddr*>(&address),
                      &reported_size) == 0);

  ConnectedPair pair;
  pair.client = Socket(socket(AF_INET, SOCK_STREAM, 0));
  REQUIRE(pair.client.valid());
  REQUIRE(connect(pair.client.get(),
                  reinterpret_cast<const sockaddr*>(&address),
                  address_size) == 0);

  pair.server = Socket(accept(listener.get(), nullptr, nullptr));
  REQUIRE(pair.server.valid());

  set_receive_timeout(pair.client);
  set_receive_timeout(pair.server);
  return pair;
}

// TCP may deliver bytes in pieces, so keep reading until we have `count`.
std::string read_exactly(Socket& socket, std::size_t count) {
  std::string result;
  char buffer[64];
  while (result.size() < count) {
    const auto received = socket.read_some(buffer, sizeof(buffer));
    REQUIRE(received > 0);
    result.append(buffer, static_cast<std::size_t>(received));
  }
  return result;
}

}  // namespace

TEST_CASE("a default-constructed Socket is empty") {
  Socket socket;
  CHECK_FALSE(socket.valid());
  CHECK(socket.get() == kInvalidSocket);
}

TEST_CASE("write_all and read_some move bytes between connected sockets") {
  auto pair = make_connected_pair();

  // Embedded NUL and CRLF: write_all must not treat data as a C string.
  const std::string message("he\0llo\r\n", 8);
  REQUIRE(pair.client.write_all(message));
  CHECK(read_exactly(pair.server, message.size()) == message);
}

TEST_CASE("read_some returns 0 once the peer has closed") {
  auto pair = make_connected_pair();
  pair.client.reset();
  CHECK_FALSE(pair.client.valid());

  char buffer[8];
  CHECK(pair.server.read_some(buffer, sizeof(buffer)) == 0);
}

TEST_CASE("the destructor closes the socket") {
  auto pair = make_connected_pair();
  {
    Socket doomed = std::move(pair.client);
  }  // doomed goes out of scope here

  char buffer[8];
  CHECK(pair.server.read_some(buffer, sizeof(buffer)) == 0);
}

TEST_CASE("moving transfers ownership without closing the connection") {
  auto pair = make_connected_pair();

  Socket moved = std::move(pair.client);
  CHECK_FALSE(pair.client.valid());
  REQUIRE(moved.valid());

  REQUIRE(moved.write_all("ping"));
  CHECK(read_exactly(pair.server, 4) == "ping");
}

TEST_CASE("move assignment closes the socket it replaces") {
  auto first = make_connected_pair();
  auto second = make_connected_pair();

  first.client = std::move(second.client);

  // first.client's old connection was closed by the assignment...
  char buffer[8];
  CHECK(first.server.read_some(buffer, sizeof(buffer)) == 0);

  // ...and the connection it took over is still alive.
  REQUIRE(first.client.write_all("hi"));
  CHECK(read_exactly(second.server, 2) == "hi");
}

TEST_CASE("self move assignment keeps the connection open") {
  auto pair = make_connected_pair();
  Socket& alias = pair.client;  // hides the self-assignment from the compiler
  pair.client = std::move(alias);

  REQUIRE(pair.client.valid());
  REQUIRE(pair.client.write_all("x"));
  CHECK(read_exactly(pair.server, 1) == "x");
}

TEST_CASE("writing to a disconnected peer fails instead of raising SIGPIPE") {
  auto pair = make_connected_pair();
  pair.server.reset();

  // The first write after the peer closed usually succeeds locally: the data
  // goes into the kernel buffer and only then does the peer answer with RST.
  // A later write gets EPIPE. If MSG_NOSIGNAL were missing, Linux would kill
  // this whole test process on that write.
  bool failed = false;
  for (int attempt = 0; attempt < 100 && !failed; ++attempt) {
    failed = !pair.client.write_all("x");
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  CHECK(failed);
}
