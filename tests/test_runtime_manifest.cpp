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

TEST_CASE("Every variant has a name fit to show an operator", "[unit]") {
  CHECK(std::string(ToString(RuntimeVariant::Stock)).find("tock") != std::string::npos);
  CHECK(std::string(ToString(RuntimeVariant::AdaPatched)).find("da") != std::string::npos);
  CHECK(std::string(ToString(RuntimeVariant::None)).length() > 0);
}
