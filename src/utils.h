#pragma once

#include "socket.h"

#include <string_view>

bool send_all(Socket socket, std::string_view response);
void handle_client(Socket client_socket);
