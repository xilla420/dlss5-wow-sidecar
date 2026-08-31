#pragma once
#include <optional>
#include <string>
#include <string_view>

#include "core/GpuProfile.h"

namespace sidecar {

// Which build of nvngx_dlssnr.dll a machine needs.
//
// The stock signed NVIDIA runtime is the only one this project will ever name a
// digest for. AdaPatched exists because the spec's GPU matrix says Ada needs a
// different build, but that build is community-produced, user-supplied, and
// never vendored or downloaded (I11) -- so the manifest recognises the shape of
// the requirement without shipping the artefact.
enum class RuntimeVariant { None, Stock, AdaPatched };

struct RuntimeEntry {
  std::string version;
  RuntimeVariant variant = RuntimeVariant::None;
};

// Known-good digests. A hit means the file is exactly a build we have seen; a
// miss means only that -- not that the file is bad. An operator may legitimately
// have a newer runtime than this table knows about, which is why the probe
// treats a miss as amber rather than red.
//
// Case-insensitive: tools print digests in both cases and an operator should not
// have to care which.
std::optional<RuntimeEntry> LookupRuntime(std::string_view sha256Hex);

// One line for a log or a dependency board. For a known build it names the
// version; for an unknown one it names the file and the digest it actually has,
// because that is what makes a bug report actionable.
std::string DescribeRuntime(std::string_view filePath, std::string_view sha256Hex);

// Spec GPU matrix (see also DefaultInternalHeight, which encodes the resolution
// half of the same table).
RuntimeVariant VariantForArchitecture(GpuArch arch);

const char* ToString(RuntimeVariant variant);

}  // namespace sidecar
