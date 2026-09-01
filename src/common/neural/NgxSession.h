#pragma once
#include <d3d12.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace sidecar {

// How a DLSS feature is configured. Kept as plain values so the shape of the
// contract is visible here rather than buried in NGX parameter names.
// DLSS render presets, as the SDK numbers them.
//
// Default is the transformer preset (K) for DLAA. The CNN presets clamp
// temporal history much harder, which matters here specifically because this
// project's motion vectors are *estimated* from two colour frames rather than
// rendered by an engine. DLSS5-Feeder, working from the same kind of estimated
// vectors, recommends exactly these when confidently-wrong vectors cause
// warping on flames, flickering lights and transparencies.
enum class DlssPreset {
  Default = 0,
  CnnE = 5,          // clamps history hardest
  CnnF = 6,
  TransformerJ = 10,
  TransformerK = 11,  // SDK default for DLAA
};

// Config files and the manager's UI carry a name, not an SDK number, so the
// numbers stay an implementation detail of this header. Both directions are
// pure and available whether or not the NGX SDK was present at configure time:
// the manager has to populate a dropdown on a machine that may have no SDK.
//
// FromName returns nullopt for an unrecognised name rather than guessing, so
// the caller can warn and fall back where it knows what the fallback is.
std::optional<DlssPreset> DlssPresetFromName(std::string_view name);
const char* DlssPresetName(DlssPreset preset);

// Every preset, in the order a menu should offer them: safest for estimated
// motion vectors first. Terminated by a null name.
struct DlssPresetChoice {
  const char* name;
  const char* description;
  DlssPreset value;
};
const DlssPresetChoice* DlssPresetChoices();

struct DlssFeatureDesc {
  uint32_t renderWidth = 0;
  uint32_t renderHeight = 0;
  uint32_t outputWidth = 0;
  uint32_t outputHeight = 0;
  bool hdr = false;
  DlssPreset preset = DlssPreset::Default;
};

// The resources a single evaluation reads and writes.
//
// depth is required. The M3 Task 4 spike established that the add-on harvests
// the depth binding out of our parameter block, so leaving it null does not
// degrade quality -- it removes the thing route B keys on. Whether a synthesised
// depth is good enough is the open question the spike could not answer, but it
// has to be *present* either way.
struct DlssEvalDesc {
  ID3D12Resource* color = nullptr;
  ID3D12Resource* motion = nullptr;
  ID3D12Resource* depth = nullptr;
  ID3D12Resource* output = nullptr;
  float jitterX = 0.0f;
  float jitterY = 0.0f;
  float motionScaleX = 1.0f;
  float motionScaleY = 1.0f;
  bool reset = false;
};

// RAII over an NGX session: init, a parameter block, and one feature.
//
// Built only when the DLSS SDK headers are available at configure time. Without
// them this compiles to a stub whose Create returns null, exactly as NvofaFlow
// does without the Optical Flow SDK -- the pipeline then runs the passthrough
// pass and says so.
//
// Linking the NGX SDK library adds no DLL import: it resolves _nvngx.dll with
// LoadLibrary at first use, which was verified against the import-table checker
// on the spike binaries. So a machine with no NGX core still loads this binary,
// and reports Available() == false rather than failing to start.
class NgxSession {
 public:
  // runtimeDir is where nvngx_*.dll live -- normally next to the executable.
  //
  // It is resolved to an absolute path before NGX sees it. A relative path is
  // accepted silently by NGX and then finds nothing, which surfaces as
  // "DLSS unavailable" and is indistinguishable from an unsupported driver.
  // That cost a wrong reading during the Task 4 spike; it is handled here once
  // so no caller can reintroduce it.
  static std::unique_ptr<NgxSession> Create(ID3D12Device* device,
                                            const std::filesystem::path& runtimeDir);
  ~NgxSession();

  NgxSession(const NgxSession&) = delete;
  NgxSession& operator=(const NgxSession&) = delete;

  bool Available() const;

  // Whether the driver reports DLSS usable at all, read from the NGX capability
  // block. False here means every CreateFeature below would fail, and the reason
  // is worth logging once at startup rather than once a frame.
  bool DlssSupported() const { return dlssSupported_; }

  // Why DLSS is unavailable, or empty when it is. Suitable for a log line.
  const std::string& UnavailableReason() const { return unavailableReason_; }

  bool CreateDlssFeature(ID3D12GraphicsCommandList* cl, const DlssFeatureDesc& desc);
  bool HasFeature() const { return handle_ != nullptr; }

  // False on any failure; never throws. The caller falls back to passthrough.
  bool Evaluate(ID3D12GraphicsCommandList* cl, const DlssEvalDesc& desc);

  // Releases the feature but keeps the session, so a resize does not pay for a
  // full NGX re-init.
  void ReleaseFeature();

 private:
  NgxSession() = default;

  ID3D12Device* device_ = nullptr;
  // Remembered from feature creation, because every evaluate has to restate them
  // as the render subrect and NGX refuses a zero one.
  uint32_t renderWidth_ = 0;
  uint32_t renderHeight_ = 0;
  bool initialised_ = false;
  bool dlssSupported_ = false;
  std::string unavailableReason_;

  // Opaque so this header never needs the NGX types, and so nothing outside the
  // .cpp can be tempted to reach into them.
  void* parameters_ = nullptr;
  void* handle_ = nullptr;
};

}  // namespace sidecar
