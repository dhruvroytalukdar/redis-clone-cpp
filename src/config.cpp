#include "config.h"

#include <charconv>
#include <string_view>

std::expected<std::uint16_t, std::string> parse_port(
    std::span<const char* const> args) {
  if (args.size() == 1) {
    return kDefaultPort;
  }

  if (args.size() != 3 || std::string_view(args[1]) != "--port") {
    return std::unexpected("Usage: redis [--port <1-65535>]");
  }

  int port = 0;
  const std::string_view argument = args[2];
  const auto [end, error] = std::from_chars(
      argument.data(), argument.data() + argument.size(), port);

  if (error != std::errc{} || end != argument.data() + argument.size() ||
      port < 1 || port > 65535) {
    return std::unexpected("Invalid port: " + std::string(argument));
  }

  return static_cast<std::uint16_t>(port);
}
