#include "save/crypto.h"

#include <algorithm>
#include <cstring>

namespace oc::crypto {

namespace {

// ------------------------------------------------------------------ AES
struct Tables {
  u8 sbox[256];
  u8 inv[256];
  Tables() {
    // S-box from the multiplicative inverse in GF(2^8) followed by the affine transform (FIPS-197 5.1.1),
    // walking the field with generator 3 (power *= 3, inverse /= 3 each step).
    unsigned power = 1, inverse = 1;
    do {
      power = (power ^ ((power << 1) & 0xFF) ^ ((power & 0x80) ? 0x1B : 0)) & 0xFF;
      inverse ^= inverse << 1;
      inverse ^= inverse << 2;
      inverse ^= inverse << 4;
      inverse &= 0xFF;
      if (inverse & 0x80) inverse ^= 0x09;
      auto rotl = [](unsigned value, int shift) { return ((value << shift) | (value >> (8 - shift))) & 0xFF; };
      const unsigned affine = inverse ^ rotl(inverse, 1) ^ rotl(inverse, 2) ^ rotl(inverse, 3) ^ rotl(inverse, 4);
      sbox[power] = static_cast<u8>(affine ^ 0x63);
    } while (power != 1);
    sbox[0] = 0x63;
    for (int i = 0; i < 256; ++i) inv[sbox[i]] = static_cast<u8>(i);
  }
};
const Tables& tab() {
  static const Tables tables;
  return tables;
}

u8 xtime(u8 value) { return static_cast<u8>((value << 1) ^ ((value & 0x80) ? 0x1B : 0)); }
u8 gmul(u8 lhs, u8 factor) {
  u8 product = 0;
  while (factor) {
    if (factor & 1) product ^= lhs;
    lhs = xtime(lhs);
    factor >>= 1;
  }
  return product;
}

void add_key(u8* state, const u8* round_key) {
  for (int i = 0; i < 16; ++i) state[i] ^= round_key[i];
}

// ------------------------------------------------------------------ SHA-1
struct Sha1 {
  u32 hash[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
  u8 buf[64] = {};
  size_t fill = 0;
  u64 total = 0;

  static u32 rol(u32 value, int shift) { return (value << shift) | (value >> (32 - shift)); }

  void block(const u8* chunk) {
    u32 schedule[80];
    for (int i = 0; i < 16; ++i)
      schedule[i] = (u32(chunk[4 * i]) << 24) | (u32(chunk[4 * i + 1]) << 16) | (u32(chunk[4 * i + 2]) << 8) | chunk[4 * i + 3];
    for (int i = 16; i < 80; ++i) schedule[i] = rol(schedule[i - 3] ^ schedule[i - 8] ^ schedule[i - 14] ^ schedule[i - 16], 1);
    u32 reg_a = hash[0], reg_b = hash[1], reg_c = hash[2], reg_d = hash[3], reg_e = hash[4];
    for (int i = 0; i < 80; ++i) {
      u32 mix, round_constant;
      if (i < 20) { mix = (reg_b & reg_c) | (~reg_b & reg_d); round_constant = 0x5A827999; }
      else if (i < 40) { mix = reg_b ^ reg_c ^ reg_d; round_constant = 0x6ED9EBA1; }
      else if (i < 60) { mix = (reg_b & reg_c) | (reg_b & reg_d) | (reg_c & reg_d); round_constant = 0x8F1BBCDC; }
      else { mix = reg_b ^ reg_c ^ reg_d; round_constant = 0xCA62C1D6; }
      const u32 temp = rol(reg_a, 5) + mix + reg_e + round_constant + schedule[i];
      reg_e = reg_d; reg_d = reg_c; reg_c = rol(reg_b, 30); reg_b = reg_a; reg_a = temp;
    }
    hash[0] += reg_a; hash[1] += reg_b; hash[2] += reg_c; hash[3] += reg_d; hash[4] += reg_e;
  }

  void update(const u8* data, size_t size) {
    total += size;
    while (size) {
      const size_t take = std::min(size, 64 - fill);
      std::memcpy(buf + fill, data, take);
      fill += take; data += take; size -= take;
      if (fill == 64) { block(buf); fill = 0; }
    }
  }

  Sha1Digest finish() {
    const u64 bits = total * 8;
    const u8 pad0 = 0x80;
    update(&pad0, 1);
    const u8 zero = 0;
    while (fill != 56) update(&zero, 1);
    u8 len[8];
    for (int i = 0; i < 8; ++i) len[i] = static_cast<u8>(bits >> (56 - 8 * i));
    update(len, 8);
    Sha1Digest digest;
    for (int i = 0; i < 5; ++i)
      for (int j = 0; j < 4; ++j) digest[4 * i + j] = static_cast<u8>(hash[i] >> (24 - 8 * j));
    return digest;
  }
};

}  // namespace

Aes256::Aes256(const u8 key[32]) {
  const auto& tables = tab();
  u8 words[60][4];
  for (int i = 0; i < 8; ++i) std::memcpy(words[i], key + 4 * i, 4);
  u8 rcon = 1;
  for (int i = 8; i < 60; ++i) {
    u8 tmp[4];
    std::memcpy(tmp, words[i - 1], 4);
    if (i % 8 == 0) {
      const u8 first = tmp[0];
      tmp[0] = static_cast<u8>(tables.sbox[tmp[1]] ^ rcon);
      tmp[1] = tables.sbox[tmp[2]];
      tmp[2] = tables.sbox[tmp[3]];
      tmp[3] = tables.sbox[first];
      rcon = xtime(rcon);
    } else if (i % 8 == 4) {
      for (auto& byte : tmp) byte = tables.sbox[byte];
    }
    for (int j = 0; j < 4; ++j) words[i][j] = static_cast<u8>(words[i - 8][j] ^ tmp[j]);
  }
  for (int round = 0; round < 15; ++round)
    for (int column = 0; column < 4; ++column) std::memcpy(rk_[round] + 4 * column, words[4 * round + column], 4);
}

void Aes256::encrypt_block(const u8 in[16], u8 out[16]) const {
  const auto& tables = tab();
  u8 state[16], next[16];
  std::memcpy(state, in, 16);
  add_key(state, rk_[0]);
  for (int round = 1; round <= 14; ++round) {
    // SubBytes + ShiftRows.
    for (int column = 0; column < 4; ++column)
      for (int row = 0; row < 4; ++row) next[4 * column + row] = tables.sbox[state[4 * ((column + row) & 3) + row]];
    if (round != 14) {
      for (int column = 0; column < 4; ++column) {
        const u8 a0 = next[4 * column], a1 = next[4 * column + 1], a2 = next[4 * column + 2], a3 = next[4 * column + 3];
        state[4 * column] = static_cast<u8>(xtime(a0) ^ xtime(a1) ^ a1 ^ a2 ^ a3);
        state[4 * column + 1] = static_cast<u8>(a0 ^ xtime(a1) ^ xtime(a2) ^ a2 ^ a3);
        state[4 * column + 2] = static_cast<u8>(a0 ^ a1 ^ xtime(a2) ^ xtime(a3) ^ a3);
        state[4 * column + 3] = static_cast<u8>(xtime(a0) ^ a0 ^ a1 ^ a2 ^ xtime(a3));
      }
    } else {
      std::memcpy(state, next, 16);
    }
    add_key(state, rk_[round]);
  }
  std::memcpy(out, state, 16);
}

void Aes256::decrypt_block(const u8 in[16], u8 out[16]) const {
  const auto& tables = tab();
  u8 state[16], next[16];
  std::memcpy(state, in, 16);
  add_key(state, rk_[14]);
  for (int round = 13; round >= 0; --round) {
    // InvShiftRows + InvSubBytes.
    for (int column = 0; column < 4; ++column)
      for (int row = 0; row < 4; ++row) next[4 * ((column + row) & 3) + row] = tables.inv[state[4 * column + row]];
    add_key(next, rk_[round]);
    if (round != 0) {
      for (int column = 0; column < 4; ++column) {
        const u8 a0 = next[4 * column], a1 = next[4 * column + 1], a2 = next[4 * column + 2], a3 = next[4 * column + 3];
        state[4 * column] = static_cast<u8>(gmul(a0, 14) ^ gmul(a1, 11) ^ gmul(a2, 13) ^ gmul(a3, 9));
        state[4 * column + 1] = static_cast<u8>(gmul(a0, 9) ^ gmul(a1, 14) ^ gmul(a2, 11) ^ gmul(a3, 13));
        state[4 * column + 2] = static_cast<u8>(gmul(a0, 13) ^ gmul(a1, 9) ^ gmul(a2, 14) ^ gmul(a3, 11));
        state[4 * column + 3] = static_cast<u8>(gmul(a0, 11) ^ gmul(a1, 13) ^ gmul(a2, 9) ^ gmul(a3, 14));
      }
    } else {
      std::memcpy(state, next, 16);
    }
  }
  std::memcpy(out, state, 16);
}

Sha1Digest sha1(ByteSpan data) {
  Sha1 hasher;
  hasher.update(data.data(), data.size());
  return hasher.finish();
}

Sha1Digest hmac_sha1(ByteSpan key, ByteSpan msg) {
  u8 key_block[64] = {};
  if (key.size() > 64) {
    const auto digest = sha1(key);
    std::memcpy(key_block, digest.data(), digest.size());
  } else if (!key.empty()) {
    std::memcpy(key_block, key.data(), key.size());
  }
  u8 ipad[64], opad[64];
  for (int i = 0; i < 64; ++i) {
    ipad[i] = key_block[i] ^ 0x36;
    opad[i] = key_block[i] ^ 0x5C;
  }
  Sha1 in;
  in.update(ipad, 64);
  in.update(msg.data(), msg.size());
  const auto inner_digest = in.finish();
  Sha1 out;
  out.update(opad, 64);
  out.update(inner_digest.data(), inner_digest.size());
  return out.finish();
}

Bytes pbkdf2_hmac_sha1(ByteSpan password, ByteSpan salt, u32 iterations, size_t out_len) {
  Bytes out;
  for (u32 block = 1; out.size() < out_len; ++block) {
    Bytes msg(salt.begin(), salt.end());
    for (int i = 3; i >= 0; --i) msg.push_back(static_cast<u8>(block >> (8 * i)));
    auto mac = hmac_sha1(password, msg);
    auto accum = mac;
    for (u32 i = 1; i < iterations; ++i) {
      mac = hmac_sha1(password, ByteSpan(mac.data(), mac.size()));
      for (size_t j = 0; j < 20; ++j) accum[j] ^= mac[j];
    }
    for (size_t j = 0; j < 20 && out.size() < out_len; ++j) out.push_back(accum[j]);
  }
  return out;
}

}  // namespace oc::crypto
