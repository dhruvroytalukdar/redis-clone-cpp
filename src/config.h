#pragma once

#include <cstdint>
#include <expected>
#include <span>
#include <string>

inline constexpr std::uint16_t kDefaultPort = 6379;

// `args` is the full argv, including the program name at index 0.
// On failure the error is a message ready to print to the user.
std::expected<std::uint16_t, std::string> parse_port(
    std::span<const char* const> args);
