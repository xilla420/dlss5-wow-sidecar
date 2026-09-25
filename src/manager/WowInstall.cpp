#include "manager/WowInstall.h"

#include <windows.h>

#include <algorithm>
#include <system_error>

#include "core/Utf8.h"

namespace fs = std::filesystem;

namespace sidecar {
namespace {

// Battle.net writes a 32-bit uninstall entry, so on an x64 manager the value
// lives under WOW6432Node. The 64-bit view is read too rather than assumed
// absent: which one a future installer writes is not ours to decide.
constexpr const wchar_t* kUninstallKey =
    LR"(SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\World of Warcraft)";

// The branches Battle.net installs under the parent folder. Retail first
// because it is what this sidecar is aimed at; the others are here so a
// classic-only install is still found rather than reported missing.
constexpr const char* kBranchFolders[] = {"_retail_", "_classic_", "_classic_era_"};

std::optional<std::wstring> ReadRegistryString(HKEY root, const wchar_t* subKey,
                                               const wchar_t* value, REGSAM view) {
  HKEY key = nullptr;
  if (RegOpenKeyExW(root, subKey, 0, KEY_READ | view, &key) != ERROR_SUCCESS) {
    return std::nullopt;
  }
  DWORD type = 0;
  DWORD bytes = 0;
  LSTATUS status = RegQueryValueExW(key, value, nullptr, &type, nullptr, &bytes);
  if (status != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) || bytes == 0) {
    RegCloseKey(key);
    return std::nullopt;
  }
  std::wstring buffer(bytes / sizeof(wchar_t) + 1, L'\0');
  status = RegQueryValueExW(key, value, nullptr, &type,
                            reinterpret_cast<LPBYTE>(buffer.data()), &bytes);
  RegCloseKey(key);
  if (status != ERROR_SUCCESS) return std::nullopt;
  // The API counts the terminator when it feels like it, so the string is cut
  // at the first NUL rather than trusted to be exactly `bytes` long.
  if (const size_t end = buffer.find(L'\0'); end != std::wstring::npos) buffer.resize(end);
  if (buffer.empty()) return std::nullopt;
  return buffer;
}

std::string ReadUninstallValue(const wchar_t* value) {
  for (const REGSAM view : {KEY_WOW64_32KEY, KEY_WOW64_64KEY}) {
    if (auto found = ReadRegistryString(HKEY_LOCAL_MACHINE, kUninstallKey, value, view)) {
      return Utf8FromWide(*found);
    }
  }
  return {};
}

// DisplayIcon carries a path in the shell's icon notation: it may be quoted,
// and it may end in a comma and an icon index. Both have to come off before it
// is a path, and neither is an error worth reporting.
std::string_view TrimIconNotation(std::string_view value) {
  while (!value.empty() && (value.front() == ' ' || value.front() == '"')) {
    value.remove_prefix(1);
  }
  // Cut at the last comma only when what follows it looks like an index, so a
  // folder with a comma in its name is not truncated.
  if (const size_t comma = value.rfind(','); comma != std::string_view::npos) {
    const std::string_view tail = value.substr(comma + 1);
    const bool index = !tail.empty() &&
                       std::all_of(tail.begin(), tail.end(), [](unsigned char c) {
                         return c == '-' || (c >= '0' && c <= '9');
                       });
    if (index) value = value.substr(0, comma);
  }
  while (!value.empty() && (value.back() == ' ' || value.back() == '"')) {
    value.remove_suffix(1);
  }
  return value;
}

void AddUnique(std::vector<std::string>& out, std::string candidate) {
  if (candidate.empty()) return;
  if (std::find(out.begin(), out.end(), candidate) != out.end()) return;
  out.push_back(std::move(candidate));
}

}  // namespace

std::vector<std::string> WowFolderCandidates(std::string_view installLocation,
                                             std::string_view displayIcon) {
  std::vector<std::string> out;

  // The executable's own folder, which is the one an injector would sit in.
  if (const std::string_view icon = TrimIconNotation(displayIcon); !icon.empty()) {
    const fs::path exe = PathFromUtf8(icon);
    if (exe.has_parent_path()) AddUnique(out, Utf8FromPath(exe.parent_path()));
  }

  if (!installLocation.empty()) {
    const fs::path root = PathFromUtf8(installLocation);
    for (const char* branch : kBranchFolders) {
      AddUnique(out, Utf8FromPath(root / branch));
    }
    // Last, and only as a fallback: the parent holds no Wow.exe, so scanning it
    // proves nothing about injectors. It is still better than nothing when a
    // future layout stops using the branch folders.
    AddUnique(out, Utf8FromPath(root));
  }

  return out;
}

std::vector<fs::path> DetectWowFolders() {
  const std::string installLocation = ReadUninstallValue(L"InstallLocation");
  const std::string displayIcon = ReadUninstallValue(L"DisplayIcon");

  std::vector<fs::path> found;
  std::error_code ec;
  for (const auto& candidate : WowFolderCandidates(installLocation, displayIcon)) {
    const fs::path path = PathFromUtf8(candidate);
    if (fs::is_directory(path, ec) && !ec) found.push_back(path);
  }
  return found;
}

std::optional<fs::path> DetectWowFolder() {
  const auto found = DetectWowFolders();
  if (found.empty()) return std::nullopt;
  return found.front();
}

}  // namespace sidecar
