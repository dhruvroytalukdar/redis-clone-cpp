#pragma once

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>

using Socket = SOCKET;
using SocketLength = int;
inline constexpr Socket kInvalidSocket = INVALID_SOCKET;
#else
#include <sys/socket.h>
#include <unistd.h>

using Socket = int;
using SocketLength = socklen_t;
inline constexpr Socket kInvalidSocket = -1;
#endif

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

void close_socket(Socket socket);
