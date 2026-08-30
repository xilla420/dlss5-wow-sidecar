#include <catch2/catch_test_macros.hpp>

TEST_CASE("test harness runs", "[unit]") {
  REQUIRE(1 + 1 == 2);
}
