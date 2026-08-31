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

// Whether a runtime can actually run on an architecture.
//
// This exists because the failure it predicts is otherwise unreadable. The
// stock runtime carries CUDA binaries built for Blackwell; on Ada it does not
// refuse at load or at init, it gets all the way to feature creation and
// returns a bare Fail with no detail. The M3 Task 4 spike hit exactly that on
// an RTX 4080 and could not tell it apart from a rejected contract. Checking
// the pair up front turns a dead end into one sentence.
enum class RuntimeCompatibility {
  Ok,
  WrongVariant,             // a real runtime, but not one this card can run
  UnsupportedArchitecture,  // the card is outside the spec's matrix entirely
};

RuntimeCompatibility CheckRuntimeCompatibility(GpuArch arch, RuntimeVariant variant);

// One sentence naming the card, the runtime, and what to do. Empty when the
// pair is fine, so callers can treat "nothing to say" as success.
std::string DescribeCompatibility(GpuArch arch, RuntimeVariant variant);

const char* ToString(RuntimeVariant variant);

}  // namespace sidecar
