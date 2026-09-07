#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sidecar {

// Finding where World of Warcraft lives, so the operator does not have to type
// it. Nothing here opens a file inside the game's folder, and nothing here is
// written: the whole module reads two registry values and does string work on
// them (I5, I8).
//
// The value worth having is the folder that holds Wow.exe -- an injector
// masquerades as a DLL the game already imports, so it has to sit beside the
// executable. Battle.net's uninstall entry records the parent instead, with
// each branch of the game in a subfolder underneath, which is why the
// candidates below are ordered from most specific to least.

// Pure. Turns Battle.net's two registry values into the folders worth trying,
// best first, without touching the filesystem. Either value may be empty.
//
// DisplayIcon is the precise one because it names Wow.exe itself. It arrives in
// the shape Windows uses for icons rather than for paths -- optionally quoted,
// optionally followed by a comma and an icon index -- so it is trimmed back to
// a path before its parent is taken.
std::vector<std::string> WowFolderCandidates(std::string_view installLocation,
                                             std::string_view displayIcon);

// The candidates that actually exist as directories on this machine, best
// first. Empty when the game is not installed, or is installed by something
// that does not register itself -- which is not an error and must not be
// reported as one.
std::vector<std::filesystem::path> DetectWowFolders();

// The single best guess, or nothing. Never invents a path: an empty result
// leaves the operator's field alone rather than filling it with a directory
// that is not there.
std::optional<std::filesystem::path> DetectWowFolder();

}  // namespace sidecar
