#pragma once
// Small, dependency-free crypto primitives needed by the RDE save container (WinZip AES-256 / AE-2):
// AES-256 (FIPS-197), SHA-1 (FIPS 180-4), HMAC-SHA1 (RFC 2104), PBKDF2-HMAC-SHA1 (RFC 2898).
// Written from the public specifications; not hardened against side channels (this is a file format tool).
#include <array>

#include "core/types.h"

namespace oc::crypto {

class Aes256 {
 public:
  explicit Aes256(const u8 key[32]);
  void encrypt_block(const u8 in[16], u8 out[16]) const;
  void decrypt_block(const u8 in[16], u8 out[16]) const;

 private:
  u8 rk_[15][16];
};

using Sha1Digest = std::array<u8, 20>;

Sha1Digest sha1(ByteSpan data);
Sha1Digest hmac_sha1(ByteSpan key, ByteSpan msg);
Bytes pbkdf2_hmac_sha1(ByteSpan password, ByteSpan salt, u32 iterations, size_t out_len);

}  // namespace oc::crypto
