#include "config.h"
#include "socket.h"
#include "utils.h"

#include <cstddef>
#include <iostream>
#include <utility>

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

int main(int argc, char* argv[]) {
  const auto port = parse_port({argv, static_cast<std::size_t>(argc)});
  if (!port) {
    std::cerr << port.error() << "\n";
    return 1;
  }

  NetworkRuntime network;
  if (!network.initialize()) {
    return 1;
  }

  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

  const Socket server_socket(socket(AF_INET, SOCK_STREAM, 0));
  if (!server_socket.valid()) {
    std::cerr << "Failed to create server socket\n";
    return 1;
  }

  const int reuse_address = 1;
  if (setsockopt(server_socket.get(), SOL_SOCKET, SO_REUSEADDR,
                 reinterpret_cast<const char*>(&reuse_address),
                 sizeof(reuse_address)) != 0) {
    std::cerr << "Failed to configure server socket\n";
    return 1;
  }

  sockaddr_in server_address{};
  server_address.sin_family = AF_INET;
  server_address.sin_addr.s_addr = htonl(INADDR_ANY);
  server_address.sin_port = htons(*port);

  if (bind(server_socket.get(),
           reinterpret_cast<const sockaddr*>(&server_address),
           sizeof(server_address)) != 0) {
    std::cerr << "Failed to bind to port " << *port << "\n";
    return 1;
  }

  constexpr int connection_backlog = 16;
  if (listen(server_socket.get(), connection_backlog) != 0) {
    std::cerr << "Failed to listen for connections\n";
    return 1;
  }

  std::cout << "Server is listening on port " << *port << "\n";

  while (true) {
    sockaddr_in client_address{};
    SocketLength client_address_length = sizeof(client_address);
    Socket client_socket(
        accept(server_socket.get(),
               reinterpret_cast<sockaddr*>(&client_address),
               &client_address_length));

    if (!client_socket.valid()) {
      std::cerr << "Failed to accept client connection\n";
      continue;
    }

    handle_client(std::move(client_socket));
  }
}
