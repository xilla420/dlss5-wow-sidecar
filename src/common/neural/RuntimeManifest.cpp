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
constexpr std::array<ManifestRow, 2> kManifest{{
    {"e16bcf15e16e13f527491cdf7845b2fe6521a738d8f7c9c721866a8496e1fc8e",
     "310.8.0", RuntimeVariant::Stock},
    // Same version and byte count as the stock build, and it still carries
    // NVIDIA's signature block -- but Authenticode reports HashMismatch,
    // because the Blackwell-built CUDA binaries inside were replaced with
    // Ada-compatible ones. Verified on an RTX 4080: with the stock build,
    // neural-rendering feature creation fails with a bare 0xbad00001; with this
    // one it creates and evaluates. That is the whole reason this row exists.
    {"e67dee209320cdafe0e93e45675d7aa34323a53acc57a72b2e40a181581c989a",
     "310.8.0", RuntimeVariant::AdaPatched},
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

RuntimeCompatibility CheckRuntimeCompatibility(GpuArch arch, RuntimeVariant variant) {
  if (arch != GpuArch::Ada && arch != GpuArch::Blackwell) {
    return RuntimeCompatibility::UnsupportedArchitecture;
  }
  switch (variant) {
    // The patched build adds Ada support without dropping Blackwell, so it is
    // the one variant that is correct on both.
    case RuntimeVariant::AdaPatched:
      return RuntimeCompatibility::Ok;
    case RuntimeVariant::Stock:
      return arch == GpuArch::Blackwell ? RuntimeCompatibility::Ok
                                        : RuntimeCompatibility::WrongVariant;
    default:
      return RuntimeCompatibility::WrongVariant;
  }
}

std::string DescribeCompatibility(GpuArch arch, RuntimeVariant variant) {
  switch (CheckRuntimeCompatibility(arch, variant)) {
    case RuntimeCompatibility::Ok:
      return {};
    case RuntimeCompatibility::UnsupportedArchitecture:
      return std::string("This GPU (") + ToString(arch) +
             ") is outside the supported matrix; neural rendering needs Ada or "
             "Blackwell.";
    case RuntimeCompatibility::WrongVariant:
    default:
      if (arch == GpuArch::Ada && variant == RuntimeVariant::Stock) {
        // The specific case worth spelling out, because the symptom is a bare
        // failure code at feature creation and nothing else says why.
        return "This is the stock runtime, which is built for Blackwell. On an "
               "Ada card neural-rendering feature creation fails with no "
               "explanation. An Ada-compatible build of nvngx_dlssnr.dll is "
               "required.";
      }
      return std::string("The ") + ToString(variant) + " runtime does not run on " +
             ToString(arch) + ".";
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
