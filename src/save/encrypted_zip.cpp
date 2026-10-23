#include "save/encrypted_zip.h"

#include <miniz.h>

#include <cstring>
#include <fstream>
#include <random>
#include <stdexcept>

#include "core/span_reader.h"
#include "save/crypto.h"

namespace oc::save {

namespace {

[[noreturn]] void fail(const std::string& message) { throw std::runtime_error("zip: " + message); }

ByteSpan span_of(std::string_view text) { return ByteSpan(reinterpret_cast<const u8*>(text.data()), text.size()); }

// WinZip AES CTR: 16-byte little-endian counter starting at 1; keystream = AES(counter); XOR. Symmetric.
void ctr_xor(const crypto::Aes256& aes, u8* data, size_t size) {
  u64 ctr = 1;
  for (size_t i = 0; i < size; i += 16) {
    u8 in[16] = {}, ks[16];
    for (int byte = 0; byte < 8; ++byte) in[byte] = static_cast<u8>(ctr >> (8 * byte));
    aes.encrypt_block(in, ks);
    for (size_t j = 0; j < 16 && i + j < size; ++j) data[i + j] ^= ks[j];
    ++ctr;
  }
}

struct Keys {
  u8 enc[32];
  Bytes mac;
  u8 verifier[2];
};

Keys derive(std::string_view password, ByteSpan salt) {
  const Bytes dk = crypto::pbkdf2_hmac_sha1(span_of(password), salt, 1000, 66);
  Keys keys;
  std::memcpy(keys.enc, dk.data(), 32);
  keys.mac.assign(dk.begin() + 32, dk.begin() + 64);
  keys.verifier[0] = dk[64];
  keys.verifier[1] = dk[65];
  return keys;
}

void put16(Bytes& out, u32 value) {
  out.push_back(static_cast<u8>(value));
  out.push_back(static_cast<u8>(value >> 8));
}
void put32(Bytes& out, u32 value) {
  put16(out, value & 0xFFFF);
  put16(out, value >> 16);
}

// Values mirrored from the RDE-written file (libzip): version made by 0x033F (Unix, 6.3), needed 20,
// external attributes 0x81B60000 (Unix mode 0100666, -rw-rw-rw-, every member of RDE's own save.dat), fixed DOS timestamp.
constexpr u32 kVersionMade = 0x033F, kVersionNeeded = 20, kExtAttr = 0x81B60000;
constexpr u32 kDosTime = 46749, kDosDate = 23882;

}  // namespace

std::vector<ZipMember> zip_open(ByteSpan buf, std::string_view password) {
  if (buf.size() < 22) fail("too small");
  size_t eocd = std::string::npos;
  for (size_t i = buf.size() - 22 + 1; i-- > 0;) {
    if (buf[i] == 'P' && buf[i + 1] == 'K' && buf[i + 2] == 5 && buf[i + 3] == 6) {
      eocd = i;
      break;
    }
    if (buf.size() - i > 22 + 65535) break;
  }
  if (eocd == std::string::npos) fail("no end-of-central-directory (truncated archive?)");
  SpanReader reader(buf);
  const u32 count = reader.at<u16>(eocd + 10);
  size_t cursor = reader.at<u32>(eocd + 16);

  std::vector<ZipMember> out;
  for (u32 index = 0; index < count; ++index) {
    if (reader.at<u32>(cursor) != 0x02014B50) fail("bad central directory entry");
    const u32 flags = reader.at<u16>(cursor + 8), method = reader.at<u16>(cursor + 10), crc = reader.at<u32>(cursor + 16);
    const u32 csize = reader.at<u32>(cursor + 20), usize = reader.at<u32>(cursor + 24);
    const u32 nlen = reader.at<u16>(cursor + 28), elen = reader.at<u16>(cursor + 30), clen = reader.at<u16>(cursor + 32);
    const size_t lho = reader.at<u32>(cursor + 42);
    ZipMember member;
    ByteSpan name_span = reader.slice(cursor + 46, nlen);
    member.name.assign(reinterpret_cast<const char*>(name_span.data()), name_span.size());
    cursor += 46 + nlen + elen + clen;

    if (reader.at<u32>(lho) != 0x04034B50) fail("bad local header for " + member.name);
    const size_t lnlen = reader.at<u16>(lho + 26), lelen = reader.at<u16>(lho + 28);
    ByteSpan extra = reader.slice(lho + 30 + lnlen, lelen);
    ByteSpan raw = reader.slice(lho + 30 + lnlen + lelen, csize);

    u32 actual = method;
    Bytes plain;
    if (method == 99) {
      u32 strength = 0;
      for (size_t pos = 0; pos + 4 <= extra.size();) {
        const u32 id = extra[pos] | (extra[pos + 1] << 8), len = extra[pos + 2] | (extra[pos + 3] << 8);
        if (id == 0x9901 && pos + 11 <= extra.size()) {
          strength = extra[pos + 8];
          actual = extra[pos + 9] | (extra[pos + 10] << 8);
        }
        pos += 4 + len;
      }
      if (strength != 3) fail(member.name + ": only AES-256 is supported");
      if (raw.size() < 16 + 2 + 10) fail(member.name + ": encrypted body too short");
      const ByteSpan salt = raw.subspan(0, 16), ver = raw.subspan(16, 2);
      const ByteSpan body = raw.subspan(18, raw.size() - 28), mac = raw.subspan(raw.size() - 10);
      const Keys keys = derive(password, salt);
      if (keys.verifier[0] != ver[0] || keys.verifier[1] != ver[1]) fail(member.name + ": wrong password (verifier)");
      const auto computed_mac = crypto::hmac_sha1(keys.mac, body);
      if (std::memcmp(computed_mac.data(), mac.data(), 10) != 0) fail(member.name + ": HMAC mismatch");
      plain.assign(body.begin(), body.end());
      ctr_xor(crypto::Aes256(keys.enc), plain.data(), plain.size());
    } else {
      if (flags & 1) fail(member.name + ": legacy ZipCrypto not supported");
      plain.assign(raw.begin(), raw.end());
    }
    if (actual == 8) {
      Bytes inflated(usize);
      const size_t got = tinfl_decompress_mem_to_mem(inflated.data(), inflated.size(), plain.data(), plain.size(), 0);
      if (got != usize) fail(member.name + ": inflate failed");
      plain = std::move(inflated);
      member.deflate = true;
    } else if (actual == 0) {
      member.deflate = false;
    } else {
      fail(member.name + ": unsupported method");
    }
    if (plain.size() != usize) fail(member.name + ": size mismatch");
    if (crc != 0) {
      if (static_cast<u32>(mz_crc32(MZ_CRC32_INIT, plain.data(), plain.size())) != crc) fail(member.name + ": CRC mismatch");
    }
    member.data = std::move(plain);
    out.push_back(std::move(member));
  }
  return out;
}

Bytes zip_write(const std::vector<ZipMember>& members, std::string_view password, u64 salt_seed) {
  std::random_device device;
  std::mt19937_64 det(salt_seed);
  auto rnd = [&]() -> u64 { return salt_seed ? det() : (static_cast<u64>(device()) << 32) ^ device(); };

  Bytes file, central;
  for (const auto& member : members) {
    const u32 usize = static_cast<u32>(member.data.size());
    // AE-2 + libzip behaviour seen in the real file: CRC stored for payloads >= 20 bytes, 0 for tiny ones.
    const u32 crc = usize >= 20 ? static_cast<u32>(mz_crc32(MZ_CRC32_INIT, member.data.data(), member.data.size())) : 0;

    Bytes body;
    if (member.deflate) {
      size_t outlen = 0;
      const int fl = static_cast<int>(tdefl_create_comp_flags_from_zip_params(9, -15, MZ_DEFAULT_STRATEGY));
      void* heap = tdefl_compress_mem_to_heap(member.data.data(), member.data.size(), &outlen, fl);
      if (!heap) fail("deflate failed for " + member.name);
      body.assign(static_cast<u8*>(heap), static_cast<u8*>(heap) + outlen);
      mz_free(heap);
    } else {
      body = member.data;
    }

    u8 salt[16];
    for (int i = 0; i < 16; i += 8) {
      const u64 word = rnd();
      for (int byte = 0; byte < 8; ++byte) salt[i + byte] = static_cast<u8>(word >> (8 * byte));
    }
    const Keys keys = derive(password, ByteSpan(salt, 16));
    ctr_xor(crypto::Aes256(keys.enc), body.data(), body.size());
    const auto mac = crypto::hmac_sha1(keys.mac, body);

    Bytes enc(salt, salt + 16);
    enc.push_back(keys.verifier[0]);
    enc.push_back(keys.verifier[1]);
    enc.insert(enc.end(), body.begin(), body.end());
    enc.insert(enc.end(), mac.begin(), mac.begin() + 10);

    // Flags: bit0 encrypted; bit1 as libzip sets it for deflate.
    const u32 flags = member.deflate ? 3 : 1;
    // AES extra field: AE-2, vendor "AE", strength 3 (AES-256).
    Bytes extra;
    put16(extra, 0x9901);
    put16(extra, 7);
    put16(extra, 2);
    extra.push_back('A');
    extra.push_back('E');
    extra.push_back(3);
    put16(extra, member.deflate ? 8 : 0);

    const u32 lho = static_cast<u32>(file.size());
    put32(file, 0x04034B50);
    put16(file, kVersionNeeded);
    put16(file, flags);
    put16(file, 99);
    put16(file, kDosTime);
    put16(file, kDosDate);
    put32(file, crc);
    put32(file, static_cast<u32>(enc.size()));
    put32(file, usize);
    put16(file, static_cast<u32>(member.name.size()));
    put16(file, static_cast<u32>(extra.size()));
    file.insert(file.end(), member.name.begin(), member.name.end());
    file.insert(file.end(), extra.begin(), extra.end());
    file.insert(file.end(), enc.begin(), enc.end());

    put32(central, 0x02014B50);
    put16(central, kVersionMade);
    put16(central, kVersionNeeded);
    put16(central, flags);
    put16(central, 99);
    put16(central, kDosTime);
    put16(central, kDosDate);
    put32(central, crc);
    put32(central, static_cast<u32>(enc.size()));
    put32(central, usize);
    put16(central, static_cast<u32>(member.name.size()));
    put16(central, static_cast<u32>(extra.size()));
    // Comment length, disk number and internal attributes, all zero.
    put16(central, 0);
    put16(central, 0);
    put16(central, 0);
    put32(central, kExtAttr);
    put32(central, lho);
    central.insert(central.end(), member.name.begin(), member.name.end());
    central.insert(central.end(), extra.begin(), extra.end());
  }
  const u32 cd_off = static_cast<u32>(file.size());
  file.insert(file.end(), central.begin(), central.end());
  put32(file, 0x06054B50);
  put16(file, 0);
  put16(file, 0);
  put16(file, static_cast<u32>(members.size()));
  put16(file, static_cast<u32>(members.size()));
  put32(file, static_cast<u32>(central.size()));
  put32(file, cd_off);
  put16(file, 0);
  return file;
}

void zip_write_file(const std::filesystem::path& path, const std::vector<ZipMember>& members,
                    std::string_view password) {
  const Bytes bytes = zip_write(members, password);
  std::filesystem::path tmp = path;
  tmp += ".tmp";
  {
    std::ofstream stream(tmp, std::ios::binary | std::ios::trunc);
    if (!stream) fail("cannot write " + tmp.string());
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!stream) fail("write failed " + tmp.string());
  }
  std::filesystem::rename(tmp, path);
}

const ZipMember* zip_find(const std::vector<ZipMember>& members, std::string_view name) {
  for (const auto& member : members)
    if (member.name == name) return &member;
  return nullptr;
}

}  // namespace oc::save
