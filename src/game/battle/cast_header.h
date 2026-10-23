#pragma once
// The chosen element's plate floating to the cast-header spot.
// Runs once per tick while the command is an Element and the menu is in target selection or waiting for the
// action start; the element list resets the state to 0 every tick, so leaving the target ring back to the list restarts it.
//   state 0: the non-selected plates fold away with the frames of kFoldFrame[a8] (a8 counts up from the list's pulse
//            index 0..3, one per tick, to 12), the selected plate stays on its row (8, 60 + 15 slot) at frame 3; when a8 > 12: a8 = 12, state 1, y = row
//   state 1: y -= 0x16 per tick; y < 0x16 -> y = 0x16, state 2 (the plate "floats quickly to the top")
//   state 2: -> state 3;  state 3: rests at (8, 0x16) = the position the cast header is drawn at (the header takes over, same plate).
namespace oc::game::bseq_hdr {

// Frame of the folding plates per a8; 0 = not drawn.
inline constexpr unsigned char kFoldFrame[13] = {4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 0};

struct FloatHeader {
  int state = 0;
  int a8 = 0;
  // y is valid once state >= 1 (state 0: the row's own y).
  int y = 0;
  // One tick. `row_y` = 0x3C + 15 * slot.
  void step(int row_y) {
    if (state == 1) {
      y -= 0x16;
      if (y < 0x16) {
        y = 0x16;
        state = 2;
      }
    } else if (state == 0) {
      if (++a8 > 12) {
        a8 = 12;
        state = 1;
        y = row_y;
      }
    } else if (state == 2) {
      state = 3;
    }
  }
  void reset(int pulse) {
    state = 0;
    a8 = pulse & 3;
    y = 0;
  }
  // y of the selected plate this tick (state 0: its list row).
  int plate_y(int row_y) const { return state == 0 ? row_y : y; }
  // frame of the other plates (0 = hidden): table while state 0, hidden afterwards (a8 stays 12).
  int fold_frame() const { return kFoldFrame[a8]; }
};

}  // namespace oc::game::bseq_hdr
