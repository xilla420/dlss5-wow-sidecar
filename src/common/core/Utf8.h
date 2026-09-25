#pragma once
#include <filesystem>
#include <string>
#include <string_view>

namespace sidecar {

// Windows stores paths as UTF-16 and this program shows them in ImGui, which
// speaks UTF-8 and nothing else. The conversion has to be explicit at that
// boundary, because the implicit one is wrong in a way that only shows up on
// somebody else's machine.
//
// std::filesystem::path's narrow accessors -- string(), and the const char*
// constructor -- go through the process's active code page, not UTF-8. On a
// machine whose code page is 1251 or 932, a path with any non-ASCII character
// in it survives neither direction: reading one out of a path and drawing it
// gives mojibake, and building a path from what ImGui hands back addresses a
// directory that does not exist. ASCII-only paths hide the bug completely,
// which is why it can sit in a Windows program for a long time.

std::wstring WideFromUtf8(std::string_view text);
std::string Utf8FromWide(std::wstring_view text);

// The path equivalents. Use these instead of path::string() and
// path(const char*) anywhere a path meets the interface or a config file.
std::filesystem::path PathFromUtf8(std::string_view text);
std::string Utf8FromPath(const std::filesystem::path& path);

}  // namespace sidecar
