#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

#include "core/Log.h"

using namespace sidecar;

TEST_CASE("a fresh log is empty", "[unit]") {
  Log log;
  REQUIRE(log.Recent().empty());
  REQUIRE(log.Dropped() == 0);
}

TEST_CASE("lines come back in the order they were written", "[unit]") {
  Log log;
  log.Write(LogLevel::Info, "first");
  log.Write(LogLevel::Info, "second");
  log.Write(LogLevel::Info, "third");

  const auto lines = log.Recent();
  REQUIRE(lines.size() == 3);
  REQUIRE(lines[0].find("first") != std::string::npos);
  REQUIRE(lines[1].find("second") != std::string::npos);
  REQUIRE(lines[2].find("third") != std::string::npos);
}

TEST_CASE("the level appears in the line so a log is greppable", "[unit]") {
  Log log;
  log.Write(LogLevel::Warn, "careful");
  log.Write(LogLevel::Error, "broken");

  const auto lines = log.Recent();
  REQUIRE(lines[0].find("WARN") != std::string::npos);
  REQUIRE(lines[1].find("ERROR") != std::string::npos);
}

TEST_CASE("the ring keeps the newest lines and counts what it dropped", "[unit]") {
  Log log;
  for (size_t i = 0; i < Log::kCapacity + 10; ++i) {
    log.Write(LogLevel::Info, "line " + std::to_string(i));
  }

  const auto lines = log.Recent();
  REQUIRE(lines.size() == Log::kCapacity);
  REQUIRE(log.Dropped() == 10);

  // Oldest surviving line is number 10, newest is the last one written.
  REQUIRE(lines.front().find("line 10") != std::string::npos);
  REQUIRE(lines.back().find("line " + std::to_string(Log::kCapacity + 9)) !=
          std::string::npos);
}

TEST_CASE("a minimum level filters quieter lines out", "[unit]") {
  Log log;
  log.SetMinimumLevel(LogLevel::Warn);
  log.Write(LogLevel::Info, "chatter");
  log.Write(LogLevel::Warn, "careful");
  log.Write(LogLevel::Error, "broken");

  const auto lines = log.Recent();
  REQUIRE(lines.size() == 2);
  REQUIRE(lines[0].find("careful") != std::string::npos);
  REQUIRE(lines[1].find("broken") != std::string::npos);
}

TEST_CASE("filtered lines are not counted as dropped", "[unit]") {
  // Dropped means "the ring overwrote it", which is a capacity problem worth
  // surfacing. A line the operator asked not to see is not a problem at all.
  Log log;
  log.SetMinimumLevel(LogLevel::Error);
  for (int i = 0; i < 50; ++i) log.Write(LogLevel::Info, "chatter");
  REQUIRE(log.Recent().empty());
  REQUIRE(log.Dropped() == 0);
}

TEST_CASE("the last error is retrievable without scanning the ring", "[unit]") {
  Log log;
  log.Write(LogLevel::Error, "first failure");
  log.Write(LogLevel::Info, "carrying on");
  log.Write(LogLevel::Error, "second failure");

  // The HUD and the manager both want "what most recently went wrong" and
  // neither should have to walk the buffer for it.
  REQUIRE(log.LastError().find("second failure") != std::string::npos);
}

TEST_CASE("last error is empty until something goes wrong", "[unit]") {
  Log log;
  log.Write(LogLevel::Info, "all fine");
  log.Write(LogLevel::Warn, "a bit off");
  REQUIRE(log.LastError().empty());
}

TEST_CASE("clear resets the ring, the drop count and the last error", "[unit]") {
  Log log;
  log.Write(LogLevel::Error, "broken");
  for (size_t i = 0; i < Log::kCapacity + 5; ++i) log.Write(LogLevel::Info, "x");

  log.Clear();
  REQUIRE(log.Recent().empty());
  REQUIRE(log.Dropped() == 0);
  REQUIRE(log.LastError().empty());
}
