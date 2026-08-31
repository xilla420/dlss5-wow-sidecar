#pragma once
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

namespace sidecar {

// SHA-256 over Windows CNG. No third-party dependency, and bcrypt.dll is a
// system DLL that every process already has -- which matters, because I11 rules
// out vendoring a crypto library for one digest.
//
// The digest is lowercase hex, 64 characters. Every failure returns an empty
// string instead, which cannot be mistaken for a real digest.

std::string Sha256Hex(const void* data, size_t size);
std::string Sha256Hex(std::string_view data);

// Streams the file in chunks. The neural runtime is ~166 MB and hashing it by
// reading it whole would spike memory for no reason.
std::string Sha256File(const std::filesystem::path& path);

}  // namespace sidecar
