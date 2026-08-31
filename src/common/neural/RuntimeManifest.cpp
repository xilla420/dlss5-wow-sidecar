#include "neural/RuntimeManifest.h"

#include <algorithm>
#include <array>
#include <cctype>

namespace sidecar {
namespace {

struct ManifestRow {
  const char* sha256;
  const char* version;
  RuntimeVariant variant;
};

// Digests are stored lowercase; LookupRuntime normalises before comparing.
//
// Only builds actually observed on hardware go in here. The 310.8.0 entry was
// confirmed twice over during the M3 Task 4 spike: renodx-dlss5.addon64 reported
// it as a reference match, and the file carries a valid NVIDIA Authenticode
// signature.
constexpr std::array<ManifestRow, 1> kManifest{{
    {"e16bcf15e16e13f527491cdf7845b2fe6521a738d8f7c9c721866a8496e1fc8e",
     "310.8.0", RuntimeVariant::Stock},
}};

std::string ToLower(std::string_view text) {
  std::string out(text);
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return out;
}

constexpr size_t kDigestChars = 64;

}  // namespace

std::optional<RuntimeEntry> LookupRuntime(std::string_view sha256Hex) {
  // Reject anything that is not digest-shaped before comparing. Without this a
  // truncated digest could prefix-match nothing and still cost a scan, and worse,
  // an empty string would read as a legitimate "no digest" lookup.
  if (sha256Hex.size() != kDigestChars) return std::nullopt;

  const std::string needle = ToLower(sha256Hex);
  for (const auto& row : kManifest) {
    if (needle == row.sha256) {
      return RuntimeEntry{row.version, row.variant};
    }
  }
  return std::nullopt;
}

std::string DescribeRuntime(std::string_view filePath, std::string_view sha256Hex) {
  const auto entry = LookupRuntime(sha256Hex);
  if (entry) {
    return std::string(filePath) + " is the " + ToString(entry->variant) +
           " runtime, version " + entry->version + ".";
  }
  if (sha256Hex.empty()) {
    return std::string(filePath) + " could not be read, so it has no digest.";
  }
  return std::string(filePath) + " is not a build this manifest knows. Its "
         "SHA-256 is " + std::string(sha256Hex) +
         " -- quote that when reporting it.";
}

RuntimeVariant VariantForArchitecture(GpuArch arch) {
  switch (arch) {
    case GpuArch::Blackwell: return RuntimeVariant::Stock;
    case GpuArch::Ada:       return RuntimeVariant::AdaPatched;
    // Turing and Ampere are refused by the spec's GPU matrix, so there is no
    // runtime to ask for rather than a runtime we happen not to have.
    default:                 return RuntimeVariant::None;
  }
}

const char* ToString(RuntimeVariant variant) {
  switch (variant) {
    case RuntimeVariant::Stock:      return "stock NVIDIA";
    case RuntimeVariant::AdaPatched: return "Ada-patched";
    default:                         return "none";
  }
}

}  // namespace sidecar
