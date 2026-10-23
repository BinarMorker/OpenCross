#pragma once
// Helpers shared by the FieldWorld translation units (field_world*.cpp). Not part of the public interface.
//
// Angles: the engine counts 4096 units per turn. Script and data angles (SET_ROTATION, MOVE headings, entrance records, GET_FACING)
// use the engine's heading, direction = (cos h, -sin h); the actor code is rotated by 0x800 from that, so angles cross the script
// boundary through to_internal() / to_script().
#include <cmath>
#include <vector>

#include "game/field_world.h"

namespace oc::game::field_internal {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kUnitsPerRad = 4096.0f / (2.0f * kPi);
constexpr float kUnitsToRad = 2.0f * kPi / 4096.0f;

constexpr int to_internal(int heading) { return (heading + 0x800) & 0xFFF; }
constexpr int to_script(int heading) { return (heading - 0x800) & 0xFFF; }

// rsin / rcos of the PS1 libraries: 4096 units per turn, 4096 = 1.0.
inline int ps1_sin(int angle) {
  return static_cast<int>(std::lround(std::sin(static_cast<double>(angle & 0xFFF) * (2.0 * kPi / 4096.0)) * 4096.0));
}
inline int ps1_cos(int angle) {
  return static_cast<int>(std::lround(std::cos(static_cast<double>(angle & 0xFFF) * (2.0 * kPi / 4096.0)) * 4096.0));
}

// Status bits of a character record: 1 poison, 8 burn, 0x10 sprain, 0x20 flu.
constexpr int kStPoison = 0x01, kStBurn = 0x08, kStSprain = 0x10, kStFlu = 0x20;

// Folder under fieldobj/player/ of every playable character id 0..43 (order of the engine's character table).
inline constexpr const char* kCharFolders[44] = {
    "selju",  "kid",     "guill",  "ishito",   "slash",   "jakotu", "riddle", "carshu", "zoah",    "malchera", "colcha",
    "lutianna", "poshul", "lazzuly", "zappa",    "ocha",    "radius", "farga",  "mamacha", "gren",   "lena",     "miki",
    "harley", "jyaness", "dragon", "hosi",      "spri",    "dan",    "kabuo",  "rablue", "jilbelt", "hone",     "kinoko",
    "ilenes", "mell",    "liia",   "banc",      "sunef",   "steena", "doc",    "gyada",  "pierre",  "olha",     "tumaru"};

// Character reference of the party / model / life ops: FD, FE, FF = the character in party slot 0, 1, 2 (FF is the THIRD member,
// unlike the entity references where FF = slot 0's entity), FC = none, 0..0x2B = that character id, anything else = none.
// Returns -1 for none / an empty slot.
inline int char_from_ref(const std::vector<int>& party, int ref) {
  if (ref >= 0xFD && ref <= 0xFF) {
    const size_t slot = static_cast<size_t>(ref - 0xFD);
    return slot < party.size() ? party[slot] : -1;
  }
  return ref >= 0 && ref < 0x2C ? ref : -1;
}

}  // namespace oc::game::field_internal
