#include "utils.h"

#include <array>
#include <iostream>

void handle_client(Socket client_socket) {
  std::array<char, 4096> buffer{};

  while (true) {
    const std::ptrdiff_t bytes_received =
        client_socket.read_some(buffer.data(), buffer.size());

    if (bytes_received == 0) {
      std::cout << "Client disconnected\n";
      break;
    }

    if (bytes_received < 0) {
      std::cerr << "Failed to receive client data\n";
      break;
    }

    std::cout << "Received " << bytes_received << " bytes\n";

    // RESP parsing will replace this temporary reply in the next milestone.
    if (!client_socket.write_all("+PONG\r\n")) {
      std::cerr << "Failed to send response\n";
      break;
    }
  }
}  // client_socket's destructor closes the connection on every path above.
