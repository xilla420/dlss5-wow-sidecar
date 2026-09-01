#include "manager/Install.h"

#include <algorithm>
#include <cctype>
#include <system_error>

namespace fs = std::filesystem;

namespace sidecar {
namespace {

std::string Lower(std::string_view text) {
  std::string out(text);
  std::transform(out.begin(), out.end(), out.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return out;
}

// The sidecar writes these itself. They are listed for the uninstaller because
// leaving a stale config and two logs behind after "remove everything" is the
// kind of tidiness failure that makes people delete the whole folder by hand.
constexpr std::string_view kGeneratedFiles[] = {
    "sidecar.toml", "sidecar.log", "sidecar-manager.log", "ReShade.ini", "ReShade.log",
};

}  // namespace

const std::vector<Component>& Components() {
  // Ordered by what a first-time operator has to do first: without the runtime
  // there is nothing to host, and without ReShade there is nothing to host it.
  static const std::vector<Component> components = {
      {"nvngx_dlssnr.dll",
       "DLSS 5 neural rendering runtime",
       "The neural network itself. On an RTX 40 card this must be a build "
       "patched for Ada; the stock runtime is Blackwell-only and fails at "
       "feature creation with no diagnostic.",
       "nvngx_dlssnr.dll",
       "github.com/rakanki911/DLSS5-Swapper releases",
       true},
      {"dxgi.dll",
       "ReShade, with add-on support",
       "Hosts the add-on inside the sidecar's own process. The add-on-enabled "
       "build is required; the plain one loads no add-ons at all.",
       "dxgi.dll,reshade64.dll,d3d11.dll",
       "reshade.me -- take the version WITH full add-on support",
       true},
      {"renodx-dlss5.addon64",
       "RenoDX DLSS 5 add-on",
       "Detours the sidecar's own NGX calls and substitutes neural-rendered "
       "output. This is what makes the pass neural rather than a plain DLAA.",
       "renodx-dlss5.addon64",
       "github.com/renodx-dev -- the DLSS 5 Generic add-on",
       true},
      {"nvngx_dlss.dll",
       "DLSS upscaling runtime",
       "Optional. Only consulted if the add-on's work-in-progress upscaling "
       "path is switched on; neural rendering itself does not need it.",
       "nvngx_dlss.dll",
       "github.com/rakanki911/DLSS5-Swapper releases",
       false},
  };
  return components;
}

bool FileMatchesComponent(const Component& component, std::string_view fileName) {
  const std::string needle = Lower(fileName);
  const std::string accepts = Lower(component.accepts);

  // Whole-token comparison, not a substring search: `nvngx_dlss.dll` is a
  // prefix of `nvngx_dlssnr.dll`, they do entirely different jobs, and swapping
  // them produces a runtime that loads and then refuses to create a feature.
  size_t start = 0;
  while (start <= accepts.size()) {
    const size_t comma = accepts.find(',', start);
    const size_t end = comma == std::string::npos ? accepts.size() : comma;
    if (accepts.compare(start, end - start, needle) == 0) return true;
    if (comma == std::string::npos) break;
    start = comma + 1;
  }
  return false;
}

size_t ComponentForFile(std::string_view fileName) {
  const auto& components = Components();
  for (size_t i = 0; i < components.size(); ++i) {
    if (FileMatchesComponent(components[i], fileName)) return i;
  }
  return static_cast<size_t>(-1);
}

InstallResult InstallComponent(const Component& component, const fs::path& source,
                               const fs::path& sidecarDir) {
  std::error_code ec;
  if (!fs::exists(source, ec) || ec) {
    return {false, "That file no longer exists."};
  }
  if (!fs::is_regular_file(source, ec) || ec) {
    return {false, "That is not a file."};
  }

  const fs::path destination = sidecarDir / std::string(component.installedAs);

  // A source that is already the destination is not an error worth a copy: the
  // filesystem would happily truncate the file to zero on the way through.
  if (fs::exists(destination, ec) && !ec && fs::equivalent(source, destination, ec) && !ec) {
    return {true, "Already installed."};
  }

  fs::copy_file(source, destination, fs::copy_options::overwrite_existing, ec);
  if (ec) {
    return {false, "Could not copy it in: " + ec.message()};
  }
  return {true, "Installed " + std::string(component.installedAs) + "."};
}

std::vector<fs::path> UninstallPlan(const fs::path& sidecarDir, bool includeGeneratedFiles) {
  std::vector<fs::path> plan;
  std::error_code ec;

  const auto add = [&](std::string_view name) {
    const fs::path path = sidecarDir / std::string(name);
    if (fs::exists(path, ec) && !ec) plan.push_back(path);
  };

  for (const auto& component : Components()) add(component.installedAs);
  if (includeGeneratedFiles) {
    for (const auto& name : kGeneratedFiles) add(name);
  }
  return plan;
}

InstallResult RemoveAll(const std::vector<fs::path>& plan) {
  size_t removed = 0;
  for (const auto& path : plan) {
    std::error_code ec;
    if (!fs::remove(path, ec) || ec) {
      // Almost always a file still mapped into a running process, which is
      // worth saying rather than reporting a bare failure.
      return {false, "Could not remove " + path.filename().string() +
                         ". Stop the overlay and close anything using it, then try again."};
    }
    ++removed;
  }
  return {true, "Removed " + std::to_string(removed) + " file(s)."};
}

}  // namespace sidecar
