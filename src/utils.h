#pragma once

#include "socket.h"

// Takes ownership of the connection; it is closed when this returns.
void handle_client(Socket client_socket);
