#include "config.h"

#include <doctest/doctest.h>

TEST_CASE("parse_port defaults to 6379 when no arguments are given") {
  const char* args[] = {"redis"};
  const auto port = parse_port(args);
  REQUIRE(port.has_value());
  CHECK(*port == 6379);
}

TEST_CASE("parse_port accepts a valid --port value") {
  const char* args[] = {"redis", "--port", "6380"};
  const auto port = parse_port(args);
  REQUIRE(port.has_value());
  CHECK(*port == 6380);
}

TEST_CASE("parse_port accepts the boundary ports 1 and 65535") {
  const char* low[] = {"redis", "--port", "1"};
  const char* high[] = {"redis", "--port", "65535"};
  CHECK(parse_port(low).value() == 1);
  CHECK(parse_port(high).value() == 65535);
}

TEST_CASE("parse_port rejects values that are not a whole in-range number") {
  for (const char* bad :
       {"0", "65536", "-1", "abc", "12abc", "", " 80", "99999999999"}) {
    CAPTURE(bad);
    const char* args[] = {"redis", "--port", bad};
    const auto port = parse_port(args);
    REQUIRE_FALSE(port.has_value());
    CHECK(port.error().starts_with("Invalid port"));
  }
}

TEST_CASE("parse_port rejects unknown flags and wrong argument counts") {
  const char* wrong_flag[] = {"redis", "--prot", "6380"};
  const char* missing_value[] = {"redis", "--port"};
  const char* extra[] = {"redis", "--port", "6380", "extra"};

  for (const auto args : {std::span<const char* const>(wrong_flag),
                          std::span<const char* const>(missing_value),
                          std::span<const char* const>(extra)}) {
    const auto port = parse_port(args);
    REQUIRE_FALSE(port.has_value());
    CHECK(port.error().starts_with("Usage"));
  }
}
