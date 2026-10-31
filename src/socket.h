#pragma once

#include <cstddef>
#include <string_view>
#include <utility>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>

using NativeSocket = SOCKET;
using SocketLength = int;
inline constexpr NativeSocket kInvalidSocket = INVALID_SOCKET;
#else
#include <sys/socket.h>
#include <unistd.h>

using NativeSocket = int;
using SocketLength = socklen_t;
inline constexpr NativeSocket kInvalidSocket = -1;
#endif

// Starts and stops Winsock on Windows; does nothing on Linux. Declare it
// before any Socket so it is destroyed after them.
class NetworkRuntime {
 public:
  NetworkRuntime() = default;
  bool initialize();
  ~NetworkRuntime();

  NetworkRuntime(const NetworkRuntime&) = delete;
  NetworkRuntime& operator=(const NetworkRuntime&) = delete;

 private:
  bool initialized_ = false;
};

// Owns one OS socket handle and closes it when the object is destroyed, on
// every return path. Move-only: exactly one Socket owns a given handle.
class Socket {
 public:
  Socket() = default;
  explicit Socket(NativeSocket handle) : handle_(handle) {}
  ~Socket() { reset(); }

  Socket(const Socket&) = delete;
  Socket& operator=(const Socket&) = delete;

  Socket(Socket&& other) noexcept
      : handle_(std::exchange(other.handle_, kInvalidSocket)) {}

  Socket& operator=(Socket&& other) noexcept {
    if (this != &other) {
      reset();
      handle_ = std::exchange(other.handle_, kInvalidSocket);
    }
    return *this;
  }

  NativeSocket get() const { return handle_; }
  bool valid() const { return handle_ != kInvalidSocket; }

  // Closes the handle now (if any) and leaves this Socket empty.
  void reset();

  // Returns bytes read; 0 means the peer closed the connection; -1 is an error.
  std::ptrdiff_t read_some(char* buffer, std::size_t length);

  // Sends every byte, looping over partial sends. False on any failure,
  // including a peer that has already disconnected (never raises SIGPIPE).
  bool write_all(std::string_view data);

 private:
  NativeSocket handle_ = kInvalidSocket;
};
