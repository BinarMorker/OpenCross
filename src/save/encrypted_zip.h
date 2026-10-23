#pragma once
// WinZip AE-2 (AES-256) ZIP reader/writer, as used by RDE's save.dat.
// Members are stored or deflated; every member written is AES-256 encrypted. Errors throw std::runtime_error.
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "core/types.h"

namespace oc::save {

// The fixed password RDE uses for save.dat.
inline constexpr std::string_view kRdeSavePassword = "376c671d-da29-44c7-a158-4a18faa5fed0";

struct ZipMember {
  std::string name;
  // Plaintext contents.
  Bytes data;
  // On write: deflate (level 9) vs stored.
  bool deflate = true;
};

// Parses and decrypts every member. Verifies the password verifier and the HMAC of encrypted members, and the
// CRC-32 when the entry carries one.
std::vector<ZipMember> zip_open(ByteSpan archive, std::string_view password = kRdeSavePassword);

// Builds an archive. salt_seed == 0 uses std::random_device; non-zero gives deterministic salts (tests).
Bytes zip_write(const std::vector<ZipMember>& members, std::string_view password = kRdeSavePassword,
                u64 salt_seed = 0);

// Writes to "<path>.tmp" then renames over `path` (replace-in-place like RDE does).
void zip_write_file(const std::filesystem::path& path, const std::vector<ZipMember>& members,
                    std::string_view password = kRdeSavePassword);

const ZipMember* zip_find(const std::vector<ZipMember>& members, std::string_view name);

}  // namespace oc::save
