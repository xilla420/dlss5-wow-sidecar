#pragma once
#include <filesystem>
#include <string>

#include "core/Config.h"
#include "core/GpuProfile.h"

namespace sidecar {

// Two builds of nvngx_dlssnr.dll can sit side by side:
//
//   runtimes\latest\nvngx_dlssnr.dll      the newest signed NVIDIA build
//   runtimes\compatible\nvngx_dlssnr.dll  the Ada-patched build, RTX 40 and 50
//
// The add-on only ever loads the copy next to the exe, and it does so when the
// first device is created, so the runtime copies the chosen one into place
// before that. With neither slot filled the loose file is used as it is, which
// keeps every install made before the slots existed working untouched.

// The file a slot holds. `slot` is Latest or Compatible; Auto has no folder.
std::filesystem::path RuntimeSlotPath(const std::filesystem::path& sidecarDir,
                                      RuntimeMode slot);

struct RuntimeChoice {
  // False when no slot holds a runtime: use the loose file and touch nothing.
  bool managed = false;
  RuntimeMode slot = RuntimeMode::Auto;
  // Set when the slot differs from what was asked for, or when it will not run
  // on this card. A fixed English sentence, so the manager can translate it and
  // the log can quote it as it is.
  const char* note = nullptr;
};

// Pure: which slot `requested` resolves to on `arch`, given which slots hold a
// runtime.
RuntimeChoice ChooseRuntime(RuntimeMode requested, GpuArch arch, bool latestPresent,
                            bool compatiblePresent);

// The same, looking at the slots on disk.
RuntimeChoice ChooseRuntime(RuntimeMode requested, GpuArch arch,
                            const std::filesystem::path& sidecarDir);

// The build number a runtime file carries in its version resource, as
// "310.9.16". It reads the version resource rather than hashing the file, so
// it says what the build claims to be, not that it is genuine -- that is the
// manifest's job. Empty when the file has none.
std::string RuntimeFileVersion(const std::filesystem::path& dll);

struct StageResult {
  bool ok = false;
  bool copied = false;  // false when the file in place was already this one
  std::string message;
};

// Puts `source` at sidecarDir\nvngx_dlssnr.dll. Skipped when the file in place
// already has the source's size and write time, which a previous stage leaves
// it with -- the latest build is 266 MB and is not worth copying every launch.
// Copies to a side file and renames over, so a failed copy never leaves a
// truncated runtime where NGX will map it.
StageResult StageRuntime(const std::filesystem::path& source,
                         const std::filesystem::path& sidecarDir);

}  // namespace sidecar
