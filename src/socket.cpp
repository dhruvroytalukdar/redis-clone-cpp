#include "socket.h"

#include <algorithm>
#include <cerrno>
#include <limits>

#ifdef _WIN32
#include <iostream>
#endif

namespace {

// Linux: without this flag, send() on a connection the peer has closed raises
// SIGPIPE, whose default action kills the whole server. With it, send() just
// fails with EPIPE. Windows has no SIGPIPE.
#if defined(MSG_NOSIGNAL)
constexpr int kSendFlags = MSG_NOSIGNAL;
#else
constexpr int kSendFlags = 0;
#endif

// Windows send/recv take an int length; clamp so a huge buffer cannot wrap
// into a negative number.
int clamp_length(std::size_t length) {
  return static_cast<int>(
      std::min<std::size_t>(length, std::numeric_limits<int>::max()));
}

// A signal can interrupt a blocking call before it transfers anything.
// That is not a real failure, so the caller should simply retry.
bool interrupted() {
#ifdef _WIN32
  return false;
#else
  return errno == EINTR;
#endif
}

}  // namespace

bool NetworkRuntime::initialize() {
#ifdef _WIN32
  WSADATA wsa_data;
  if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
    std::cerr << "Failed to initialize Winsock\n";
    return false;
  }
#endif

  initialized_ = true;
  return true;
}

NetworkRuntime::~NetworkRuntime() {
#ifdef _WIN32
  if (initialized_) {
    WSACleanup();
  }
#endif
}

void Socket::reset() {
  if (handle_ == kInvalidSocket) {
    return;
  }

  // Not retried on EINTR: after close() the descriptor is gone either way.
#ifdef _WIN32
  closesocket(handle_);
#else
  close(handle_);
#endif
  handle_ = kInvalidSocket;
}

std::ptrdiff_t Socket::read_some(char* buffer, std::size_t length) {
  while (true) {
    const auto received = recv(handle_, buffer, clamp_length(length), 0);
    if (received < 0 && interrupted()) {
      continue;
    }
    return received;
  }
}

bool Socket::write_all(std::string_view data) {
  while (!data.empty()) {
    const auto sent =
        send(handle_, data.data(), clamp_length(data.size()), kSendFlags);

    if (sent < 0 && interrupted()) {
      continue;
    }
    if (sent <= 0) {
      return false;
    }

    data.remove_prefix(static_cast<std::size_t>(sent));
  }

  return true;
}
