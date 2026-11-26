#include "logloom/version.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("liblogloom version is non-empty and matches project version", "[version][smoke]") {
  const auto ver = logloom::version();
  REQUIRE_FALSE(ver.empty());
  REQUIRE(ver == "1.0.0");
}
