#include "platform/keymap.h"

#include <SDL3/SDL_scancode.h>

namespace oc::platform {

namespace {

struct Binding {
  int keys[2];
  int vks[2];
};
constexpr Binding kBindings[static_cast<int>(Action::Count)] = {
    {{SDL_SCANCODE_UP, SDL_SCANCODE_W}, {0x26, 0x57}},
    {{SDL_SCANCODE_DOWN, SDL_SCANCODE_S}, {0x28, 0x53}},
    {{SDL_SCANCODE_LEFT, SDL_SCANCODE_A}, {0x25, 0x41}},
    {{SDL_SCANCODE_RIGHT, SDL_SCANCODE_D}, {0x27, 0x44}},
    {{SDL_SCANCODE_RETURN, SDL_SCANCODE_X}, {0x0D, 0x58}},
    {{SDL_SCANCODE_BACKSPACE, SDL_SCANCODE_LSHIFT}, {0x08, 0xA0}},
    {{SDL_SCANCODE_F, SDL_SCANCODE_R}, {0x46, 0x52}},
    {{SDL_SCANCODE_I, SDL_SCANCODE_TAB}, {0x49, 0x09}},
    {{SDL_SCANCODE_H, SDL_SCANCODE_Q}, {0x48, 0x51}},
    {{SDL_SCANCODE_J, SDL_SCANCODE_E}, {0x4A, 0x45}},
    {{SDL_SCANCODE_F1, 0}, {0x70, 0}},
    {{SDL_SCANCODE_F2, 0}, {0x71, 0}},
};

const Binding& binding(Action action) { return kBindings[static_cast<int>(action)]; }

}  // namespace

std::array<int, 2> action_keys(Action action) { return {binding(action).keys[0], binding(action).keys[1]}; }

bool action_down(const Input& in, Action action) {
  for (int key : binding(action).keys)
    if (key > 0 && in.key_down[static_cast<size_t>(key)]) return true;
  return false;
}

bool action_pressed(const Input& in, Action action) {
  for (int key : binding(action).keys)
    if (key > 0 && in.key_pressed[static_cast<size_t>(key)]) return true;
  return false;
}

std::array<int, 2> action_vks(Action action) { return {binding(action).vks[0], binding(action).vks[1]}; }

int action_vk(Action action) { return binding(action).vks[0]; }

int keyboard_cell(int vk) {
  struct Cell {
    u8 vk;
    u8 index;
  };
  static constexpr Cell cells[] = {
      {0x08, 51}, {0x09, 50}, {0x0D, 49}, {0x13, 66}, {0x14, 62}, {0x1B, 48}, {0x20, 1},   {0x21, 58}, {0x22, 59},  {0x23, 61},  {0x24, 60},
      {0x25, 55}, {0x26, 57}, {0x27, 54}, {0x28, 56}, {0x2C, 65}, {0x2D, 52}, {0x2E, 53},  {0x30, 7},  {0x31, 8},   {0x32, 9},   {0x33, 10},
      {0x34, 11}, {0x35, 12}, {0x36, 13}, {0x37, 14}, {0x38, 15}, {0x39, 16}, {0x41, 19},  {0x42, 20}, {0x43, 21},  {0x44, 22},  {0x45, 23},
      {0x46, 24}, {0x47, 25}, {0x48, 26}, {0x49, 27}, {0x4A, 28}, {0x4B, 29}, {0x4C, 30},  {0x4D, 31}, {0x4E, 32},  {0x4F, 33},  {0x50, 34},
      {0x51, 35}, {0x52, 36}, {0x53, 37}, {0x54, 38}, {0x55, 39}, {0x56, 40}, {0x57, 41},  {0x58, 42}, {0x59, 43},  {0x5A, 44},  {0x5B, 98},
      {0x5C, 102}, {0x5D, 103}, {0x60, 79}, {0x61, 80}, {0x62, 81}, {0x63, 82}, {0x64, 83}, {0x65, 84}, {0x66, 85}, {0x67, 86},  {0x68, 87},
      {0x69, 88}, {0x6A, 91}, {0x6B, 93}, {0x6C, 94}, {0x6D, 92}, {0x6E, 89}, {0x6F, 90},  {0x70, 67}, {0x71, 68},  {0x72, 69},  {0x73, 70},
      {0x74, 71}, {0x75, 72}, {0x76, 73}, {0x77, 74}, {0x78, 75}, {0x79, 76}, {0x7A, 77},  {0x7B, 78}, {0x90, 64},  {0x91, 63},  {0xA0, 95},
      {0xA1, 99}, {0xA2, 96}, {0xA3, 100}, {0xA4, 97}, {0xA5, 101}, {0xBA, 2}, {0xBB, 17}, {0xBC, 3},  {0xBD, 4},   {0xBE, 5},   {0xBF, 6},
      {0xC0, 45}, {0xDB, 46}, {0xDD, 47}, {0xDE, 18}};
  for (const Cell& cell : cells)
    if (cell.vk == vk) return cell.index;
  return 0;
}

}  // namespace oc::platform
