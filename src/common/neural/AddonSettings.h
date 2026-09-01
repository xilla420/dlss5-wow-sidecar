#pragma once
#include <filesystem>
#include <string>
#include <string_view>

#include "core/Config.h"

namespace sidecar {

// The bridge between the manager's sliders and the RenoDX DLSS 5 add-on.
//
// The add-on has no API. It reads its configuration out of ReShade.ini's
// [RenoDX.DLSS5] section once, when ReShade loads it, which happens inside our
// own process at first device creation. So "apply a setting" means "write the
// file before that happens", and nothing else.
//
// That file does not belong to us. ReShade writes its own sections to it, the
// add-on writes keys we do not model, and the operator may have set things
// through ReShade's own overlay. So the transform below is a *merge*: it
// rewrites the handful of keys the manager owns and leaves every other byte
// where it was.

// Pure, and therefore testable without a ReShade installation anywhere near the
// test machine. Returns `ini` with the add-on's section brought in line with
// `settings`, and with [ADDON] AddonPath set, which is what makes ReShade look
// for the add-on beside the executable at all.
//
// Unknown sections, unknown keys, comments, blank lines, the byte-order mark and
// the file's dominant line ending all survive unchanged.
std::string ApplyNeuralSettings(std::string_view ini, const NeuralSettings& settings);

// Reads, transforms, writes. A missing file is not an error -- it is the normal
// first run, and produces a file containing exactly the two sections we own.
// False means the file could not be written, which the caller must report:
// silently failing here would leave the operator adjusting sliders that do
// nothing.
bool WriteNeuralSettings(const std::filesystem::path& iniPath,
                         const NeuralSettings& settings);

}  // namespace sidecar
