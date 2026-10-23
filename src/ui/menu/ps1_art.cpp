#include "ui/menu/ps1_art.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "import/container.h"
#include "import/tim.h"

namespace oc::ui::ps1 {
namespace {

// Face file stems by character id (face\1999\NN_<stem>.png in the HD art).
const char* const kFaces[50] = {"seljyu", "kid",   "gill",  "ishit",  "srash",  "jakotu", "ridel",  "karsh", "zoa",   "mal",
                                "col",    "luchi", "poshul", "raz",   "zappa",  "orcha",  "radius", "cap",   "oba",   "glen",
                                "rena",   "miki",  "harler", "janes", "ryu",    "star",   "spriggan", "lucky", "kabuo", "rablue",
                                "jilbelt", "skarl", "kinoko", "ningyo", "mell",  "rea",    "banklif", "sunef", "miko",  "doc",
                                "gyadaran", "yusha", "olha",  "C13base", "C13a", "C13b",   "C13aa",  "C13ab", "C13bb", "yama"};

Rgba decode(const import::Tim& tim) {
  Rgba out;
  auto img = import::decode_tim(tim);
  if (!img) return out;
  const import::Image rgba = import::to_rgba(*img);
  out.w = rgba.width, out.h = rgba.height, out.px = rgba.pixels;
  return out;
}

}  // namespace

const char* face_name(int id) { return id >= 0 && id < 50 ? kFaces[id] : nullptr; }

std::u32string bitmap_text(const import::JpGlyphs& glyphs, std::u32string text) {
  for (char32_t& ch : text) {
    if (ch == U' ' || ch == U'\n' || glyphs.index_for(ch) >= 0) continue;
    char32_t alt[3] = {0, 0, 0};
    // The JP font has curly quotes and full-width ASCII, not the Latin forms.
    if (ch == U'"') alt[0] = 0x201C, alt[1] = 0xFF02;
    else if (ch == U',') alt[0] = 0xFF0C, alt[1] = 0x3001;
    else if (ch == U'\'') alt[0] = 0xFF07, alt[1] = 0x2019;
    else if (ch >= 0x21 && ch <= 0x7E) alt[0] = 0xFF01 + (ch - 0x21);
    else if (ch == 0x2018 || ch == 0x2019) alt[0] = 0xFF07, alt[1] = 0x27;
    else if (ch == 0x201C || ch == 0x201D) alt[0] = 0xFF02, alt[1] = 0x22;
    else if (ch == 0x2026) alt[0] = 0x22EF, alt[1] = 0x2026;
    else if (ch == 0x2010 || ch == 0x2013 || ch == 0x2014) alt[0] = 0x30FC, alt[1] = 0xFF0D;
    for (char32_t candidate : alt)
      if (candidate && glyphs.index_for(candidate) >= 0) {
        ch = candidate;
        break;
      }
  }
  return text;
}

Rgba parchment(const vfs::Vfs& fs) {
  auto raw = fs.read_disc("cdrom/map/etc/systim.bin");
  if (!raw) return {};
  for (size_t i = 0; i + 32 < raw->size(); i += 4) {
    if (std::memcmp(raw->data() + i, "\x10\0\0\0", 4) != 0) continue;
    auto tim = import::parse_tim(ByteSpan(raw->data() + i, raw->size() - i));
    if (tim && tim->bpp == 4 && tim->has_clut && tim->image.x == 896 && tim->image.y == 96 && tim->image.w == 16 && tim->image.h == 64) return decode(*tim);
  }
  return {};
}

Rgba menutim(const vfs::Vfs& fs, std::string_view entry) {
  auto raw = fs.read_disc("cdrom/menu/menutim.prd");
  if (!raw) return {};
  auto drp = import::parse_drp(ByteSpan(raw->data(), raw->size()));
  if (!drp) return {};
  for (const auto& drp_entry : drp->entries)
    if (drp_entry.name == entry) {
      if (auto tim = import::parse_tim(drp_entry.data)) return decode(*tim);
    }
  return {};
}

Rgba face(const vfs::Vfs& fs, int char_id) {
  const char* stem = face_name(char_id);
  if (!stem) return {};
  auto raw = fs.read_disc(std::string("cdrom/face/") + stem + ".tim");
  if (!raw) return {};
  auto tim = import::parse_tim(ByteSpan(raw->data(), raw->size()));
  return tim ? decode(*tim) : Rgba{};
}

Rgba portrait(const vfs::Vfs& fs, int char_id) {
  if (char_id < 0 || char_id > 49) return {};
  char path[48];
  std::snprintf(path, sizeof path, "cdrom/menu/playtim/p%02d.tim", char_id);
  auto raw = fs.read_disc(path);
  if (!raw) return {};
  auto tim = import::parse_tim(ByteSpan(raw->data(), raw->size()));
  return tim ? decode(*tim) : Rgba{};
}

Rgba slot_icon(const vfs::Vfs& fs, int icon) {
  if (icon < 0 || icon > 14) return {};
  static const char kHex[] = "0123456789abcdef";
  // The sheet name uses icon + 1.
  const char name[5] = {'p', kHex[icon + 1], 'i', 'c', 0};
  return menutim(fs, name);
}

namespace {
// Copies the opaque texels (alpha != 0) of src (sx, sy, w, h) to dst at (dx, dy).
void blit(Rgba& dst, const Rgba& src, int sx, int sy, int w, int h, int dx, int dy) {
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      const int tx = sx + x, ty = sy + y, ox = dx + x, oy = dy + y;
      if (tx < 0 || ty < 0 || tx >= static_cast<int>(src.w) || ty >= static_cast<int>(src.h)) continue;
      if (ox < 0 || oy < 0 || ox >= static_cast<int>(dst.w) || oy >= static_cast<int>(dst.h)) continue;
      const u8* texel = src.at(static_cast<u32>(tx), static_cast<u32>(ty));
      if (texel[3] == 0) continue;
      std::memcpy(&dst.px[(static_cast<size_t>(oy) * dst.w + static_cast<size_t>(ox)) * 4], texel, 4);
    }
}
}  // namespace

Rgba plate(const vfs::Vfs& fs, int type, int variant) {
  const char* colour = type == 8 ? "mnbl" : type == 9 ? "mngr" : type == 10 ? "mnku" : type == 11 ? "mnre" : type == 12 ? "mnwh" : type == 13 ? "mnye" : type == 38 ? "mnni" : nullptr;
  if (!colour) return {};
  const Rgba frame_sheet = menutim(fs, colour), strip = menutim(fs, "mpla");
  if (!frame_sheet || !strip) return {};
  Rgba out;
  out.w = 184, out.h = 24;
  out.px.assign(static_cast<size_t>(out.w) * out.h * 4, 0);
  // The last part of every frame, drawn first.
  blit(out, strip, 1, 0, 173, 24, 5, 0);
  struct Part {
    int u, v, w, h, x, y;
  };
  static constexpr Part kF1[4] = {{40, 0, 24, 16, 4, 0}, {40, 16, 16, 8, 4, 16}, {64, 0, 16, 16, 163, 0}, {56, 16, 24, 8, 155, 16}};
  static constexpr Part kF2[4] = {{80, 0, 24, 16, 4, 0}, {80, 16, 8, 8, 4, 16}, {104, 0, 16, 16, 163, 0}, {96, 16, 24, 8, 155, 16}};
  static constexpr Part kF4[4] = {{0, 0, 24, 12, 4, 0}, {0, 12, 12, 12, 4, 12}, {16, 12, 24, 12, 155, 12}, {28, 0, 12, 12, 167, 0}};
  const Part* parts = variant == 2 ? kF2 : variant == 4 ? kF4 : kF1;
  for (int i = 0; i < 4; ++i) blit(out, frame_sheet, parts[i].u, parts[i].v, parts[i].w, parts[i].h, parts[i].x, parts[i].y);
  return out;
}

Rgba keyitem(const vfs::Vfs& fs, int nn) {
  // cdrom file index 152 + NN (research data/cdrom_index.csv rows 153..216; 206..212 do not exist) = the order of hd/menu/keyitem/NN.png
  // (checked by matching the 4x-reduced PNGs against every TIM by mean colour error: 42 of the 57 PNGs pick the TIM of their own index
  // first; the error is large for all pairs, 70..190 per channel, so the order of the index is the rule and the match only supports it).
  static const char* const kNames[64] = {
      "tele",  "shark",  "kid",    "suzu",   "musibue", "ilurans", "icegun", "ice",   "hekeran", "rindo",  "egg",   "gaikotu", "sebone",
      "rokkotu", "kotuban", "kenko", "hone",  "kaori",   "kinoko",  "jakotu", "hidora", "kyakuma", "chikaro", "ryu",  "white",   "black",
      "red",   "blue",   "yellow", "green",  "seimei",  "uroko",   "hoshi",  "cardkey", "bomb",  "ryu2",   "ryu3",  "pass",    "kajitu",
      "katami", "hundle", "mirror", "tate",   "sio",     "chrono",  "soubi",  "broti", "gekijo",  "monsho", "book",  "igaku",   "badge",
      "tokiegg", nullptr, nullptr,  nullptr,  nullptr,   nullptr,   nullptr,  nullptr, "gofu",    "time",   "kajiya", "uroko"};
  if (nn < 1 || nn > 64 || !kNames[nn - 1]) return {};
  auto raw = fs.read_disc(std::string("cdrom/menu/keyitem/") + kNames[nn - 1] + ".tim");
  if (!raw) return {};
  auto tim = import::parse_tim(ByteSpan(raw->data(), raw->size()));
  return tim ? decode(*tim) : Rgba{};
}

}  // namespace oc::ui::ps1
