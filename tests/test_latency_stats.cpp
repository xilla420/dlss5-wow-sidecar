#include <catch2/catch_test_macros.hpp>
#include "core/LatencyStats.h"

using sidecar::LatencyStats;

TEST_CASE("percentiles of an empty window are zero", "[unit]") {
  LatencyStats s;
  REQUIRE(s.Count() == 0);
  REQUIRE(s.P50() == 0.0);
  REQUIRE(s.P99() == 0.0);
}

TEST_CASE("median of a known ramp", "[unit]") {
  LatencyStats s;
  for (int i = 1; i <= 100; ++i) s.Record(static_cast<double>(i));
  REQUIRE(s.Count() == 100);
  REQUIRE(s.P50() == 50.0);
  REQUIRE(s.P99() == 99.0);
}

TEST_CASE("p99 tracks the tail rather than the mean", "[unit]") {
  LatencyStats s;
  // 99 samples, so the single spike occupies the 99th nearest rank and p99 has
  // to surface it. At 100 samples that spike would be the 100th value instead,
  // and a correct p99 would report 10.0 -- which is what the ramp case above
  // pins down.
  for (int i = 0; i < 98; ++i) s.Record(10.0);
  s.Record(500.0);
  REQUIRE(s.Count() == 99);
  REQUIRE(s.P50() == 10.0);
  REQUIRE(s.P99() == 500.0);
}

TEST_CASE("the ring keeps only the most recent samples", "[unit]") {
  LatencyStats s;
  for (int i = 0; i < 2000; ++i) s.Record(1.0);
  for (int i = 0; i < 1024; ++i) s.Record(7.0);
  REQUIRE(s.Count() == 1024);
  REQUIRE(s.P50() == 7.0);
}

TEST_CASE("drops are counted separately from samples", "[unit]") {
  LatencyStats s;
  s.Record(5.0);
  s.RecordDrop();
  s.RecordDrop();
  REQUIRE(s.Count() == 1);
  REQUIRE(s.Dropped() == 2);
}

TEST_CASE("reset clears samples and drops", "[unit]") {
  LatencyStats s;
  s.Record(5.0);
  s.RecordDrop();
  s.Reset();
  REQUIRE(s.Count() == 0);
  REQUIRE(s.Dropped() == 0);
  REQUIRE(s.P50() == 0.0);
}
