#include "utils.h"

#include <array>
#include <iostream>

bool send_all(Socket socket, std::string_view response) {
  std::size_t sent = 0;

  while (sent < response.size()) {
    const int bytes_sent = send(
        socket,
        response.data() + sent,
        static_cast<int>(response.size() - sent),
        0);

    if (bytes_sent <= 0) {
      return false;
    }

    sent += static_cast<std::size_t>(bytes_sent);
  }

  return true;
}

void handle_client(Socket client_socket) {
  std::array<char, 4096> buffer{};

  while (true) {
    const int bytes_received = recv(
        client_socket, buffer.data(), static_cast<int>(buffer.size()), 0);

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
    if (!send_all(client_socket, "+PONG\r\n")) {
      std::cerr << "Failed to send response\n";
      break;
    }
  }

  close_socket(client_socket);
}
