#pragma once
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace sidecar {

// Installing, for this project, means one thing: putting three files the
// operator already has next to wowsidecar.exe under the names it looks for.
//
// It cannot mean anything more. None of the three is ours to redistribute, and
// I10 forbids either binary from doing any networking at all, so nothing here
// downloads, checks for updates, or contacts anything. The manager's job is to
// say precisely which file is missing, where that file comes from, and then to
// copy the one the operator points it at.
//
// Uninstalling is the exact inverse, and is deliberately narrow: it removes only
// the files this list names, from the sidecar's own directory. It never touches
// a WoW installation -- that is the whole safety claim, and an uninstaller is
// the last place to make an exception to it.

struct Component {
  // What it must be called for the sidecar to find it.
  std::string_view installedAs;
  std::string_view title;
  std::string_view purpose;
  // Source filenames recognised when the operator browses for it, lowercased
  // and comma-separated. ReShade in particular ships under several names
  // depending on how it was obtained.
  std::string_view accepts;
  // Where a person gets this. Text, shown for copying -- never fetched.
  std::string_view source;
  bool required;
};

const std::vector<Component>& Components();

// Pure. Case-insensitive, and matches the whole filename rather than a
// substring, so `nvngx_dlss.dll` is never mistaken for `nvngx_dlssnr.dll` --
// the two differ by two characters and only one of them does neural rendering.
bool FileMatchesComponent(const Component& component, std::string_view fileName);

// Pure. The component a browsed file belongs to, or npos when it is none of
// them. Used to accept a whole folder without asking which file is which.
size_t ComponentForFile(std::string_view fileName);

struct InstallResult {
  bool ok = false;
  std::string message;
};

// Copies `source` to `sidecarDir/component.installedAs`, overwriting. Refuses a
// source inside the sidecar directory itself, which would otherwise be a
// self-copy that silently truncates the file.
InstallResult InstallComponent(const Component& component,
                               const std::filesystem::path& source,
                               const std::filesystem::path& sidecarDir);

// Everything InstallComponent could have put in the directory, plus the files
// the sidecar generates for itself, filtered to what actually exists. Returned
// rather than acted on so the manager can show the operator the list before
// anything is deleted.
std::vector<std::filesystem::path> UninstallPlan(const std::filesystem::path& sidecarDir,
                                                 bool includeGeneratedFiles);

// Deletes exactly the paths given. Reports how many went, and names the first
// one that would not.
InstallResult RemoveAll(const std::vector<std::filesystem::path>& plan);

}  // namespace sidecar
