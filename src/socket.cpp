#include "socket.h"

#ifdef _WIN32
#include <iostream>
#endif

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

void close_socket(Socket socket) {
#ifdef _WIN32
  closesocket(socket);
#else
  close(socket);
#endif
}
