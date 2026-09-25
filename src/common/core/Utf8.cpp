#include "core/Utf8.h"

#include <windows.h>

namespace sidecar {
namespace {

// The Win32 conversions want an int length and reject a null pointer even for
// an empty range, so the empty case is answered before either is involved.
template <typename Out>
Out Empty() {
  return Out{};
}

}  // namespace

std::wstring WideFromUtf8(std::string_view text) {
  if (text.empty()) return Empty<std::wstring>();
  const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(),
                                       static_cast<int>(text.size()), nullptr, 0);
  if (size <= 0) return Empty<std::wstring>();
  std::wstring out(static_cast<size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                      out.data(), size);
  return out;
}

std::string Utf8FromWide(std::wstring_view text) {
  if (text.empty()) return Empty<std::string>();
  const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                       nullptr, 0, nullptr, nullptr);
  if (size <= 0) return Empty<std::string>();
  std::string out(static_cast<size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size,
                      nullptr, nullptr);
  return out;
}

std::filesystem::path PathFromUtf8(std::string_view text) {
  return std::filesystem::path(WideFromUtf8(text));
}

std::string Utf8FromPath(const std::filesystem::path& path) {
  return Utf8FromWide(path.native());
}

}  // namespace sidecar
