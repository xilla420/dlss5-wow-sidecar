#include <catch2/catch_test_macros.hpp>

#include <string>

#include "neural/RuntimeManifest.h"

using namespace sidecar;

// Pure lookups over a hash string, so none of this needs a runtime binary
// present, a GPU, or NVIDIA's SDK.

TEST_CASE("A known runtime digest resolves to its build", "[unit]") {
  // Reported as a reference match by renodx-dlss5.addon64 against the signed
  // NVIDIA 310.8.0 build, and independently reproduced by Sha256File in the
  // Task 4 spike.
  const auto entry = LookupRuntime(
      "e16bcf15e16e13f527491cdf7845b2fe6521a738d8f7c9c721866a8496e1fc8e");
  REQUIRE(entry.has_value());
  CHECK(entry->version == "310.8.0");
  CHECK(entry->variant == RuntimeVariant::Stock);
}

// Verified on hardware: with this build the neural-rendering feature creates
// and evaluates on an RTX 4080, where the stock build fails outright.
TEST_CASE("The Ada-patched runtime resolves to its own variant", "[unit]") {
  const auto entry = LookupRuntime(
      "e67dee209320cdafe0e93e45675d7aa34323a53acc57a72b2e40a181581c989a");
  REQUIRE(entry.has_value());
  CHECK(entry->version == "310.8.0");
  CHECK(entry->variant == RuntimeVariant::AdaPatched);
}

// The two builds share a version string and a byte count, so nothing but the
// digest tells them apart. If these ever collide the manifest is useless.
TEST_CASE("The stock and Ada-patched builds are distinguished", "[unit]") {
  const auto stock = LookupRuntime(
      "e16bcf15e16e13f527491cdf7845b2fe6521a738d8f7c9c721866a8496e1fc8e");
  const auto patched = LookupRuntime(
      "e67dee209320cdafe0e93e45675d7aa34323a53acc57a72b2e40a181581c989a");
  REQUIRE(stock.has_value());
  REQUIRE(patched.has_value());
  CHECK(stock->variant != patched->variant);
}

TEST_CASE("Digest matching ignores case", "[unit]") {
  // The add-on prints uppercase, Sha256Hex produces lowercase, and an operator
  // pasting either into a bug report should get the same answer.
  const auto lower = LookupRuntime(
      "e16bcf15e16e13f527491cdf7845b2fe6521a738d8f7c9c721866a8496e1fc8e");
  const auto upper = LookupRuntime(
      "E16BCF15E16E13F527491CDF7845B2FE6521A738D8F7C9C721866A8496E1FC8E");
  REQUIRE(lower.has_value());
  REQUIRE(upper.has_value());
  CHECK(lower->version == upper->version);
  CHECK(lower->variant == upper->variant);
}

TEST_CASE("An unrecognised digest resolves to nothing", "[unit]") {
  CHECK_FALSE(LookupRuntime(std::string(64, 'a')).has_value());
}

TEST_CASE("A malformed digest resolves to nothing", "[unit]") {
  CHECK_FALSE(LookupRuntime("").has_value());
  CHECK_FALSE(LookupRuntime("not a digest").has_value());
  CHECK_FALSE(LookupRuntime("e16bcf15").has_value());  // truncated
}

// The message is the whole point of the manifest: an operator with a runtime we
// do not know must be told which file we looked at and what it actually hashed
// to, or they cannot report it usefully.
TEST_CASE("Describing an unknown runtime names the file and its digest", "[unit]") {
  const std::string digest(64, 'b');
  const std::string message = DescribeRuntime("C:\\sidecar\\nvngx_dlssnr.dll", digest);
  CHECK(message.find("nvngx_dlssnr.dll") != std::string::npos);
  CHECK(message.find(digest) != std::string::npos);
}

TEST_CASE("Describing a known runtime names its version", "[unit]") {
  const std::string message = DescribeRuntime(
      "C:\\sidecar\\nvngx_dlssnr.dll",
      "e16bcf15e16e13f527491cdf7845b2fe6521a738d8f7c9c721866a8496e1fc8e");
  CHECK(message.find("310.8.0") != std::string::npos);
}

// Task 6's GPU matrix depends on this: Blackwell takes the stock runtime, Ada
// needs the patched one. Getting it backwards selects a runtime that cannot run
// on the card, which is one candidate cause of the feature-18 failure Task 4
// could not separate -- so it is pinned here rather than assumed.
TEST_CASE("Each architecture asks for the variant it can run", "[unit]") {
  CHECK(VariantForArchitecture(GpuArch::Blackwell) == RuntimeVariant::Stock);
  CHECK(VariantForArchitecture(GpuArch::Ada) == RuntimeVariant::AdaPatched);
}

TEST_CASE("Unsupported architectures ask for no runtime at all", "[unit]") {
  CHECK(VariantForArchitecture(GpuArch::Turing) == RuntimeVariant::None);
  CHECK(VariantForArchitecture(GpuArch::Ampere) == RuntimeVariant::None);
  CHECK(VariantForArchitecture(GpuArch::Unsupported) == RuntimeVariant::None);
}

// The pairing that cost the Task 4 spike a day of ambiguity. The stock runtime
// carries Blackwell-built CUDA binaries; on Ada it loads, initialises, and then
// fails at feature creation with a bare error code and nothing else to go on.
// Checking the pair up front is the whole point.
TEST_CASE("The stock runtime on an Ada card is refused with an explanation",
          "[unit]") {
  CHECK(CheckRuntimeCompatibility(GpuArch::Ada, RuntimeVariant::Stock) ==
        RuntimeCompatibility::WrongVariant);

  const std::string why = DescribeCompatibility(GpuArch::Ada, RuntimeVariant::Stock);
  CHECK(why.empty() == false);
  // It has to name both halves of the mismatch, or the operator cannot act.
  CHECK(why.find("Blackwell") != std::string::npos);
  CHECK(why.find("Ada") != std::string::npos);
}

TEST_CASE("Runtimes that can run on a card are accepted", "[unit]") {
  CHECK(CheckRuntimeCompatibility(GpuArch::Blackwell, RuntimeVariant::Stock) ==
        RuntimeCompatibility::Ok);
  CHECK(CheckRuntimeCompatibility(GpuArch::Ada, RuntimeVariant::AdaPatched) ==
        RuntimeCompatibility::Ok);
  // The patched build adds Ada without dropping Blackwell, so it is correct on
  // both rather than being an Ada-only substitute.
  CHECK(CheckRuntimeCompatibility(GpuArch::Blackwell, RuntimeVariant::AdaPatched) ==
        RuntimeCompatibility::Ok);
}

TEST_CASE("A compatible pairing has nothing to say", "[unit]") {
  CHECK(DescribeCompatibility(GpuArch::Blackwell, RuntimeVariant::Stock).empty());
  CHECK(DescribeCompatibility(GpuArch::Ada, RuntimeVariant::AdaPatched).empty());
}

TEST_CASE("Cards outside the matrix are reported as such, not as wrong runtimes",
          "[unit]") {
  for (GpuArch arch : {GpuArch::Turing, GpuArch::Ampere, GpuArch::Unsupported}) {
    CHECK(CheckRuntimeCompatibility(arch, RuntimeVariant::AdaPatched) ==
          RuntimeCompatibility::UnsupportedArchitecture);
    CHECK(DescribeCompatibility(arch, RuntimeVariant::AdaPatched).empty() == false);
  }
}

TEST_CASE("Every variant has a name fit to show an operator", "[unit]") {
  CHECK(std::string(ToString(RuntimeVariant::Stock)).find("tock") != std::string::npos);
  CHECK(std::string(ToString(RuntimeVariant::AdaPatched)).find("da") != std::string::npos);
  CHECK(std::string(ToString(RuntimeVariant::None)).length() > 0);
}
