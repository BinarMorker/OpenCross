#pragma once
// Dialogue page layout and the typewriter state machine (no rendering, no GPU types).
#include <functional>
#include <vector>

#include "import/text.h"

namespace oc::ui {

struct LayoutGlyph {
  char32_t ch = 0;
  // Top-left of the pen box, relative to the text origin; color is the {color(n)} index.
  float x = 0, y = 0;
  u8 color = 0;
  bool italic = false;
};

struct PageEvent {
  enum class Kind : u8 { Delay, Speed, Wait, FWait, AutoClose, FSet };
  Kind kind = Kind::Delay;
  // Fires before glyph `at` is revealed (== glyph count: after the last one).
  u32 at = 0;
  int arg = 0;
};

struct TextPage {
  std::vector<LayoutGlyph> glyphs;
  std::vector<PageEvent> events;
  // width: widest line. ends_message is false when a {bpage} follows.
  float width = 0;
  int lines = 0;
  bool ends_message = true;
};

struct LayoutMetrics {
  // advance: pen advance of one glyph. max_width 0 = never wrap. wrap: automatic word wrap (non-JP).
  std::function<float(char32_t)> advance;
  float line_height = 0;
  float max_width = 0;
  bool wrap = false;
  // Choice mode (non-JP; the message is laid out with a choice flag once the script marks it as a choice): a line that
  // starts with three spaces is a choice line; its indent run is laid out with special space widths - the first space (it and the
  // next two all spaces) w0 = 70, the others w0 = 20, advance = 2 w0 * 0.45 canvas units at glyph scale 0.5 - and the line is
  // never wrapped. choice_space_first / choice_space are those advances at the caller's scale (0 = rule off).
  float choice_space_first = 0, choice_space = 0;
};

// Splits a flattened message (import::flatten output) into pages: lines, wrapping, events positioned by glyph index.
std::vector<TextPage> layout_pages(const import::Message& flat, const LayoutMetrics& metrics);

// Typewriter pacing: `speed` / 64 glyphs per 60 Hz tick (0x80 = 2 per tick, 1 per tick when a message sets no {speed});
// {wait(n)} / {wend(n)} count 30 Hz field frames.
constexpr float kSpeedTickHz = 60.0f;
constexpr float kScriptFrameHz = 30.0f;
constexpr int kDefaultSpeed = 64;

class Typewriter {
 public:
  enum class State : u8 { Idle, Revealing, WaitConfirm, WaitPage, WaitClose, Closing, Closed };

  void start(std::vector<TextPage> pages);
  // Same message laid out again (other font metrics): keeps the page, the revealed count (clamped), events and state.
  void replace_pages(std::vector<TextPage> pages);
  void update(float dt);
  // Confirm key: advances a wait / page / close; while revealing it fast-forwards to the next wait.
  void confirm();

  State state() const { return state_; }
  bool closed() const { return state_ == State::Closed; }
  bool waiting_for_input() const {
    return state_ == State::WaitConfirm || state_ == State::WaitPage || state_ == State::WaitClose;
  }
  size_t page_index() const { return page_; }
  size_t page_count() const { return pages_.size(); }
  u32 visible() const { return visible_; }
  const TextPage* page() const { return page_ < pages_.size() ? &pages_[page_] : nullptr; }

  // Field event register = script variable 0x22: {fset(n)} writes n to it when the reveal reaches it; {fwait(n)} holds the reveal
  // there until it equals n. Scripts poll / set it to sync with the text (Kid collapsing at {fset(3)}, the window closing after the
  // script answers 1). Unset (the demo): both are ignored.
  std::function<int()> event_get;
  std::function<void(int)> event_set;

 private:
  // One 1/60 s tick.
  void step();
  void begin_page();

  std::vector<TextPage> pages_;
  size_t page_ = 0;
  u32 visible_ = 0;
  size_t ev_ = 0;
  State state_ = State::Idle;
  int speed_ = kDefaultSpeed;
  // acc_: glyph accumulator. delay_s_: pause left. tick_acc_: time carried between update() calls.
  // auto_close_: frames until close, -1 = none.
  float acc_ = 0;
  float delay_s_ = 0;
  float tick_acc_ = 0;
  int auto_close_ = -1;
  float close_s_ = 0;
  bool fast_ = false;
};

}  // namespace oc::ui
