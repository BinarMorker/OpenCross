#include "ui/ps1_window.h"

#include <cstdio>
#include <string>

#include "import/tim.h"

namespace oc::ui::ps1win {

// Always the disc file: RDE blanks map/window/* in cdrom2, which Vfs::read() prefers even in original mode.
ps1::Rgba skin(const vfs::Vfs& fs, int file_n) {
  char path[48];
  std::snprintf(path, sizeof path, "cdrom/map/window/w%d.tim", file_n);
  return disc_tim(fs, path);
}

ps1::Rgba disc_tim(const vfs::Vfs& fs, const std::string& path) {
  ps1::Rgba out;
  auto raw = fs.read_disc(path);
  if (!raw) return out;
  auto tim = import::parse_tim(ByteSpan(raw->data(), raw->size()));
  if (!tim) return out;
  auto img = import::decode_tim(*tim);
  if (!img) return out;
  const import::Image rgba = import::to_rgba(*img);
  out.w = rgba.width, out.h = rgba.height, out.px = rgba.pixels;
  return out;
}

}  // namespace oc::ui::ps1win
