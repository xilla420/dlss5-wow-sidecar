#include "neural/RuntimeSelect.h"

#include <windows.h>

#include <vector>

#include <system_error>

namespace fs = std::filesystem;

namespace sidecar {
namespace {

constexpr const char* kRuntimeFile = "nvngx_dlssnr.dll";

bool HasFile(const fs::path& path) {
  std::error_code ec;
  return fs::is_regular_file(path, ec) && !ec;
}

}  // namespace

fs::path RuntimeSlotPath(const fs::path& sidecarDir, RuntimeMode slot) {
  return sidecarDir / "runtimes" /
         (slot == RuntimeMode::Compatible ? "compatible" : "latest") / kRuntimeFile;
}

RuntimeChoice ChooseRuntime(RuntimeMode requested, GpuArch arch, bool latestPresent,
                            bool compatiblePresent) {
  RuntimeChoice choice;
  if (!latestPresent && !compatiblePresent) return choice;
  choice.managed = true;

  // Only Blackwell can run the stock build. Anything else that reaches here is
  // Ada, because the runtime refuses older cards before it asks.
  const bool blackwell = arch == GpuArch::Blackwell;

  switch (requested) {
    case RuntimeMode::Latest:
      if (!blackwell && compatiblePresent) {
        choice.slot = RuntimeMode::Compatible;
        choice.note = "The latest runtime runs only on RTX 50 cards, so the "
                      "compatible one is used on this card.";
      } else if (!latestPresent) {
        choice.slot = RuntimeMode::Compatible;
        choice.note = "The latest runtime is not installed, so the compatible one "
                      "is used.";
      } else {
        choice.slot = RuntimeMode::Latest;
        if (!blackwell) {
          choice.note = "Only the latest runtime is installed, and it runs only on RTX 50 "
                        "cards. Install the compatible one for this card.";
        }
      }
      return choice;

    case RuntimeMode::Compatible:
      if (compatiblePresent) {
        choice.slot = RuntimeMode::Compatible;
      } else {
        choice.slot = RuntimeMode::Latest;
        choice.note = blackwell
            ? "The compatible runtime is not installed, so the latest one is used."
            : "Only the latest runtime is installed, and it runs only on RTX 50 "
              "cards. Install the compatible one for this card.";
      }
      return choice;

    case RuntimeMode::Auto:
    default:
      if (blackwell && latestPresent) {
        choice.slot = RuntimeMode::Latest;
      } else if (compatiblePresent) {
        choice.slot = RuntimeMode::Compatible;
      } else {
        choice.slot = RuntimeMode::Latest;
        choice.note = "Only the latest runtime is installed, and it runs only on RTX 50 "
                      "cards. Install the compatible one for this card.";
      }
      return choice;
  }
}

RuntimeChoice ChooseRuntime(RuntimeMode requested, GpuArch arch, const fs::path& sidecarDir) {
  return ChooseRuntime(requested, arch,
                       HasFile(RuntimeSlotPath(sidecarDir, RuntimeMode::Latest)),
                       HasFile(RuntimeSlotPath(sidecarDir, RuntimeMode::Compatible)));
}

std::string RuntimeFileVersion(const fs::path& dll) {
  DWORD ignored = 0;
  const DWORD size = GetFileVersionInfoSizeW(dll.c_str(), &ignored);
  if (size == 0) return {};
  std::vector<unsigned char> block(size);
  if (!GetFileVersionInfoW(dll.c_str(), 0, size, block.data())) return {};
  VS_FIXEDFILEINFO* info = nullptr;
  UINT length = 0;
  if (!VerQueryValueW(block.data(), L"\\", reinterpret_cast<void**>(&info), &length) ||
      !info || length < sizeof(VS_FIXEDFILEINFO)) {
    return {};
  }
  // The fourth part is always zero on these builds and only adds noise.
  return std::to_string(HIWORD(info->dwFileVersionMS)) + "." +
         std::to_string(LOWORD(info->dwFileVersionMS)) + "." +
         std::to_string(HIWORD(info->dwFileVersionLS));
}

StageResult StageRuntime(const fs::path& source, const fs::path& sidecarDir) {
  StageResult result;
  std::error_code ec;
  const fs::path target = sidecarDir / kRuntimeFile;

  const auto sourceSize = fs::file_size(source, ec);
  if (ec) {
    result.message = "could not read " + source.string() + ": " + ec.message();
    return result;
  }
  const auto sourceTime = fs::last_write_time(source, ec);
  if (ec) {
    result.message = "could not read " + source.string() + ": " + ec.message();
    return result;
  }

  if (HasFile(target)) {
    std::error_code sizeEc, timeEc;
    const auto targetSize = fs::file_size(target, sizeEc);
    const auto targetTime = fs::last_write_time(target, timeEc);
    if (!sizeEc && !timeEc && targetSize == sourceSize && targetTime == sourceTime) {
      result.ok = true;
      result.message = "already in place";
      return result;
    }
  }

  fs::path staging = target;
  staging += ".staging";
  fs::copy_file(source, staging, fs::copy_options::overwrite_existing, ec);
  if (ec) {
    fs::remove(staging, ec);
    result.message = "could not copy " + source.string() + ": " + ec.message();
    return result;
  }
  // The comparison above depends on it, and copy_file does not promise it.
  fs::last_write_time(staging, sourceTime, ec);
  fs::rename(staging, target, ec);
  if (ec) {
    // Almost always the old runtime still mapped by another process that
    // loaded ReShade from this folder. The old file stays and is used.
    const std::string why = ec.message();
    fs::remove(staging, ec);
    result.message = "could not replace " + target.string() + ": " + why;
    return result;
  }
  result.ok = true;
  result.copied = true;
  result.message = "copied " + source.string();
  return result;
}

}  // namespace sidecar
