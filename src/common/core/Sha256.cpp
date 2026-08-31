#include "core/Sha256.h"

#include <windows.h>
#include <bcrypt.h>

#include <array>
#include <fstream>
#include <vector>

namespace sidecar {
namespace {

constexpr size_t kDigestBytes = 32;
constexpr size_t kChunkBytes = 1 << 20;  // 1 MiB

std::string ToHex(const unsigned char* bytes, size_t size) {
  static constexpr char kDigits[] = "0123456789abcdef";
  std::string out;
  out.reserve(size * 2);
  for (size_t i = 0; i < size; ++i) {
    out.push_back(kDigits[bytes[i] >> 4]);
    out.push_back(kDigits[bytes[i] & 0x0F]);
  }
  return out;
}

// RAII over the two CNG handles. They must be closed in the opposite order to
// their creation, and there are enough early returns below that doing it by
// hand would eventually leak one.
class HashContext {
 public:
  bool Open() {
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&algorithm_,
                                                    BCRYPT_SHA256_ALGORITHM,
                                                    nullptr, 0))) {
      algorithm_ = nullptr;
      return false;
    }
    if (!BCRYPT_SUCCESS(BCryptCreateHash(algorithm_, &hash_, nullptr, 0,
                                         nullptr, 0, 0))) {
      hash_ = nullptr;
      return false;
    }
    return true;
  }

  bool Update(const void* data, size_t size) {
    // BCryptHashData takes a ULONG length, so a buffer larger than 4 GiB has to
    // go in pieces. Nothing here is that large today, but the loop costs a line
    // and removes the possibility of a silent truncation later.
    const auto* p = static_cast<const unsigned char*>(data);
    while (size > 0) {
      const ULONG piece =
          static_cast<ULONG>(size > 0x40000000u ? 0x40000000u : size);
      if (!BCRYPT_SUCCESS(BCryptHashData(hash_, const_cast<PUCHAR>(p), piece, 0))) {
        return false;
      }
      p += piece;
      size -= piece;
    }
    return true;
  }

  std::string Finish() {
    std::array<unsigned char, kDigestBytes> digest{};
    if (!BCRYPT_SUCCESS(BCryptFinishHash(hash_, digest.data(),
                                         static_cast<ULONG>(digest.size()), 0))) {
      return {};
    }
    return ToHex(digest.data(), digest.size());
  }

  ~HashContext() {
    if (hash_) BCryptDestroyHash(hash_);
    if (algorithm_) BCryptCloseAlgorithmProvider(algorithm_, 0);
  }

 private:
  BCRYPT_ALG_HANDLE algorithm_ = nullptr;
  BCRYPT_HASH_HANDLE hash_ = nullptr;
};

}  // namespace

std::string Sha256Hex(const void* data, size_t size) {
  HashContext ctx;
  if (!ctx.Open()) return {};
  if (!ctx.Update(data, size)) return {};
  return ctx.Finish();
}

std::string Sha256Hex(std::string_view data) {
  return Sha256Hex(data.data(), data.size());
}

std::string Sha256File(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return {};

  HashContext ctx;
  if (!ctx.Open()) return {};

  std::vector<char> chunk(kChunkBytes);
  while (in) {
    in.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
    const auto read = static_cast<size_t>(in.gcount());
    if (read == 0) break;
    if (!ctx.Update(chunk.data(), read)) return {};
  }
  // Distinguish "reached the end" from "the read failed part way through": a
  // partial hash presented as a digest would be worse than no digest at all.
  if (in.bad()) return {};

  return ctx.Finish();
}

}  // namespace sidecar
