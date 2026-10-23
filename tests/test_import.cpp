// Self-contained checks for the container and LZSS decoders using hand-built buffers.
#include <cstdio>
#include <cstring>

#include "import/container.h"
#include "import/lzss.h"

using namespace oc;

namespace {
int failures = 0;
#define CHECK(x)                                                        \
  do {                                                                  \
    if (!(x)) {                                                         \
      std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #x);           \
      ++failures;                                                       \
    }                                                                   \
  } while (0)

void put32(Bytes& out, u32 value) {
  for (int i = 0; i < 4; ++i) out.push_back(static_cast<u8>(value >> (8 * i)));
}

// cpt with an EOF pointer (count 2, two offsets, eof: 16-byte header, entries "AAAA" and "BB") and the variant without one
// (count 2, two offsets, data at once).
void test_cpt() {
  Bytes with_eof;
  put32(with_eof, 2);
  put32(with_eof, 16);
  put32(with_eof, 20);
  put32(with_eof, 22);
  with_eof.insert(with_eof.end(), {'A', 'A', 'A', 'A', 'B', 'B'});
  auto cpt = import::parse_cpt(with_eof);
  CHECK(cpt.has_value());
  CHECK(cpt->has_eof_pointer);
  CHECK(cpt->entries.size() == 2);
  CHECK(cpt->entries[0].size == 4 && cpt->entries[0].data[0] == 'A');
  CHECK(cpt->entries[1].size == 2 && cpt->entries[1].data[1] == 'B');

  Bytes without_eof;
  put32(without_eof, 2);
  put32(without_eof, 12);
  put32(without_eof, 15);
  without_eof.insert(without_eof.end(), {'X', 'Y', 'Z', 'W'});
  auto cpt2 = import::parse_cpt(without_eof);
  CHECK(cpt2.has_value());
  CHECK(!cpt2->has_eof_pointer);
  CHECK(cpt2->entries.size() == 2);
  CHECK(cpt2->entries[0].size == 3 && cpt2->entries[1].size == 1 && cpt2->entries[1].data[0] == 'W');
}

// drp with one entry "tex0" of type 4 and 16 payload bytes: the header's offset word points at the subfile header, whose
// length field stores 16 * the payload size (0x100).
void test_drp() {
  Bytes drp_bytes = {'d', 'r', 'p', 0, 0, 0, 0, 0, 64, 0, 0, 0};
  put32(drp_bytes, 16);
  const Bytes subfile_header = {0, 0, 0, 0, 't', 'e', 'x', '0', 4, 0x00, 0x01, 0};
  drp_bytes.insert(drp_bytes.end(), subfile_header.begin(), subfile_header.end());
  for (int i = 0; i < 16; ++i) drp_bytes.push_back(static_cast<u8>(i));
  auto drp = import::parse_drp(drp_bytes);
  CHECK(drp.has_value());
  CHECK(drp->entries.size() == 1);
  CHECK(drp->entries[0].name == "tex0");
  CHECK(drp->entries[0].type == 4);
  CHECK(drp->entries[0].data.size() == 16 && drp->entries[0].data[15] == 15);
}

// Bit writer for building an LZSS stream: literals and back-references as the decoder expects them.
struct BitWriter {
  Bytes out;
  u32 accumulator = 0;
  int pending_bits = 0;
  void bits(u32 value, int count) {
    for (int i = count - 1; i >= 0; --i) {
      accumulator = (accumulator << 1) | ((value >> i) & 1u);
      if (++pending_bits == 8) {
        out.push_back(static_cast<u8>(accumulator));
        accumulator = 0;
        pending_bits = 0;
      }
    }
  }
  void flush() {
    while (pending_bits != 0) bits(0, 1);
  }
};

// Four literals "ABAB" then a back-reference to ring position 1 with count field 2 (= 4 bytes): the write pointer starts
// at 1, so "ABAB" sits at ring 1..4 and the copy doubles it.
void test_lzss() {
  BitWriter writer;
  for (char literal : {'A', 'B', 'A', 'B'}) {
    writer.bits(1, 1);
    writer.bits(static_cast<u32>(literal), 8);
  }
  writer.bits(0, 1);
  writer.bits(1, 12);
  writer.bits(2, 4);
  writer.flush();
  Bytes file = {'s', 's', 'z', 'l'};
  put32(file, 8);
  put32(file, 0);
  file.insert(file.end(), writer.out.begin(), writer.out.end());
  auto dec = import::lzss_decode(file);
  CHECK(dec.has_value());
  CHECK(dec->size() == 8);
  CHECK(std::memcmp(dec->data(), "ABABABAB", 8) == 0);
  CHECK(!import::lzss_decode(Bytes{1, 2, 3}).has_value());
}
}  // namespace

int main() {
  test_cpt();
  test_drp();
  test_lzss();
  std::printf(failures ? "%d failure(s)\n" : "ok\n", failures);
  return failures ? 1 : 0;
}
