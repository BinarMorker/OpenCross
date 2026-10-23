#pragma once
// PS1 window skins of the field and the menus, decoded from the disc: map/window/wN.tim, an 8 bpp 128 x 96 TIM (the frame pieces at
// the top, the 16 x 16 hand frames at v 80, u = frame * 16). The field uses w4, w1, w2, w3, w5 .. w15 for option bytes 0..14; the menus
// use w17.tim. The 4x redraws hd/window/wNN.png share the layout, so one set of normalised UVs addresses both.
#include <string>

#include "ui/menu/ps1_art.h"
#include "vfs/vfs.h"

namespace oc::ui::ps1win {

// Skin sheet N (cdrom/map/window/wN.tim). Transparent where the CLUT entry is 0x0000.
ps1::Rgba skin(const vfs::Vfs& fs, int file_n);
// Any TIM read from cdrom.dat itself (Vfs::read_disc: the cdrom2 copies of some PS1 textures are blank), decoded to RGBA with
// CLUT row 0; empty if missing or malformed.
ps1::Rgba disc_tim(const vfs::Vfs& fs, const std::string& path);

}  // namespace oc::ui::ps1win
