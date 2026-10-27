#include "ui/typewriter.h"

#include <algorithm>

namespace oc::ui {

using import::TokKind;

namespace {

// Lays glyphs out line by line at the metrics' advances. Word wrap (non-JP) breaks at the last space of the line when a glyph would
// cross max_width, moving the glyphs after that space to the next line and dropping the space itself; a choice line (three-space
// indent in choice mode) is never wrapped. Events are recorded at the glyph index they fire before.
struct PageBuilder {
  const LayoutMetrics& metrics;
  std::vector<TextPage> pages;
  TextPage cur;
  float x = 0, y = 0;
  int line_start = 0;
  // Index into cur.glyphs of the last space on this line.
  int last_space = -1;
  u8 color = 0;
  bool italic = false;
  // choice_line: this line starts with the three-space choice indent (choice mode only);
  // in_indent: still inside that line's leading space run.
  bool choice_line = false;
  bool in_indent = false;
  // {input(n)}: the next digits_left glyphs are number-entry digits, the next one being digit_next.
  int digits_left = 0;
  int digit_next = 0;

  explicit PageBuilder(const LayoutMetrics& layout_metrics) : metrics(layout_metrics) {}

  float advance(char32_t ch) const { return metrics.advance ? metrics.advance(ch) : 0.0f; }

  void new_line() {
    x = 0;
    y += metrics.line_height;
    line_start = static_cast<int>(cur.glyphs.size());
    last_space = -1;
    choice_line = in_indent = false;
  }

  // advance_override >= 0 replaces the font advance (the choice indent).
  void add_glyph(char32_t ch, float advance_override = -1.0f) {
    const float glyph_advance = advance_override >= 0 ? advance_override : advance(ch);
    if (metrics.wrap && metrics.max_width > 0 && x > 0 && x + glyph_advance > metrics.max_width && ch != U' ' && !choice_line) {
      if (last_space >= line_start) {
        std::vector<LayoutGlyph> moved(cur.glyphs.begin() + last_space + 1, cur.glyphs.end());
        cur.glyphs.resize(static_cast<size_t>(last_space));
        new_line();
        for (LayoutGlyph glyph : moved) {
          glyph.x = x;
          glyph.y = y;
          x += advance(glyph.ch);
          cur.glyphs.push_back(glyph);
        }
      } else {
        new_line();
      }
    }
    LayoutGlyph glyph;
    glyph.ch = ch;
    glyph.x = x;
    glyph.y = y;
    glyph.color = color;
    glyph.italic = italic;
    if (digits_left > 0) {
      glyph.digit = digit_next++;
      --digits_left;
    }
    cur.glyphs.push_back(glyph);
    if (ch == U' ') last_space = static_cast<int>(cur.glyphs.size()) - 1;
    x += glyph_advance;
  }

  void event(PageEvent::Kind kind, int arg) {
    cur.events.push_back({kind, static_cast<u32>(cur.glyphs.size()), arg});
  }

  void finish_page(bool ends_message) {
    cur.ends_message = ends_message;
    float widest = 0;
    int lines = 0;
    for (const LayoutGlyph& glyph : cur.glyphs) {
      widest = std::max(widest, glyph.x + advance(glyph.ch));
      if (metrics.line_height > 0) lines = std::max(lines, static_cast<int>(glyph.y / metrics.line_height + 0.5f) + 1);
    }
    cur.width = widest;
    cur.lines = lines;
    pages.push_back(std::move(cur));
    cur = TextPage();
    x = y = 0;
    line_start = 0;
    last_space = -1;
    choice_line = in_indent = false;
  }
};

char32_t button_glyph(int button) {
  switch (button) {
    // Circle, cross, triangle, square.
    case 0: return 0x25CB;
    case 1: return 0x00D7;
    case 2: return 0x25B3;
    case 3: return 0x25A1;
    default: return U'?';
  }
}

}  // namespace

std::vector<TextPage> layout_pages(const import::Message& flat, const LayoutMetrics& metrics) {
  PageBuilder builder(metrics);
  bool ended = false;
  for (const import::Token& token : flat.tokens) {
    if (ended) break;
    switch (token.kind) {
      case TokKind::Text:
        for (size_t i = 0; i < token.text.size(); ++i) {
          const char32_t ch = token.text[i];
          const auto space_at = [&](size_t j) { return j < token.text.size() && token.text[j] == U' '; };
          if (metrics.choice_space_first > 0) {
            if (static_cast<int>(builder.cur.glyphs.size()) == builder.line_start && space_at(i) && space_at(i + 1) && space_at(i + 2))
              builder.choice_line = builder.in_indent = true;
            if (builder.in_indent && ch == U' ') {
              builder.add_glyph(ch, space_at(i + 1) && space_at(i + 2) ? metrics.choice_space_first : metrics.choice_space);
              continue;
            }
            builder.in_indent = false;
          }
          builder.add_glyph(ch);
        }
        break;
      case TokKind::Newline: builder.new_line(); break;
      case TokKind::Page: builder.finish_page(false); break;
      case TokKind::Wait: builder.event(PageEvent::Kind::Wait, 0); break;
      case TokKind::Delay: builder.event(PageEvent::Kind::Delay, token.arg); break;
      case TokKind::Speed: builder.event(PageEvent::Kind::Speed, token.arg); break;
      case TokKind::AutoClose: builder.event(PageEvent::Kind::AutoClose, token.arg); break;
      case TokKind::End: ended = true; break;
      case TokKind::Color: builder.color = static_cast<u8>(token.arg); break;
      case TokKind::Italic: builder.italic = token.arg != 0; break;
      case TokKind::Button: builder.add_glyph(button_glyph(token.arg)); break;
      case TokKind::FWait: builder.event(PageEvent::Kind::FWait, token.arg); break;
      case TokKind::FSet: builder.event(PageEvent::Kind::FSet, token.arg); break;
      case TokKind::Input:
        builder.digits_left = std::clamp(token.arg, 0, 8);
        builder.digit_next = 0;
        break;
      default: break;
    }
  }
  builder.finish_page(true);
  // An empty page that only follows a {bpage} adds nothing to show.
  if (builder.pages.size() > 1 && builder.pages.back().glyphs.empty() && builder.pages.back().events.empty()) {
    builder.pages.pop_back();
    builder.pages.back().ends_message = true;
  }
  return std::move(builder.pages);
}

// ---------------------------------------------------------------------------------------------- Typewriter
void Typewriter::start(std::vector<TextPage> pages) {
  pages_ = std::move(pages);
  page_ = 0;
  speed_ = kDefaultSpeed;
  auto_close_ = -1;
  tick_acc_ = 0;
  state_ = pages_.empty() ? State::Closed : State::Revealing;
  begin_page();
}

void Typewriter::replace_pages(std::vector<TextPage> pages) {
  if (pages.empty()) return;
  pages_ = std::move(pages);
  if (page_ >= pages_.size()) page_ = pages_.size() - 1;
  const TextPage& page = pages_[page_];
  visible_ = std::min<u32>(visible_, static_cast<u32>(page.glyphs.size()));
  ev_ = std::min(ev_, page.events.size());
}

void Typewriter::begin_page() {
  visible_ = 0;
  ev_ = 0;
  acc_ = 0;
  delay_s_ = 0;
  fast_ = false;
}

void Typewriter::update(float dt) {
  tick_acc_ += dt;
  const float tick = 1.0f / kSpeedTickHz;
  int guard = 0;
  while (tick_acc_ >= tick && guard++ < 600) {
    tick_acc_ -= tick;
    step();
  }
  if (guard >= 600) tick_acc_ = 0;
}

// One 1/60 s tick of the reveal: a running delay counts down first; then every event due before the next glyph runs (a delay or
// wait stops the tick, an unsatisfied {fwait} is retried next tick); then glyphs are revealed - all of them at once at speed 255 or
// after a fast-forward confirm, else speed / 64 per tick, stopping early when the next event comes due. A fully revealed page waits
// for the page turn, starts its {wend} countdown or waits to close.
void Typewriter::step() {
  const float tick = 1.0f / kSpeedTickHz;
  if (state_ == State::Closing) {
    close_s_ -= tick;
    if (close_s_ <= 0) state_ = State::Closed;
    return;
  }
  if (state_ != State::Revealing) return;
  const TextPage& page = pages_[page_];
  if (delay_s_ > 0 && !fast_) {
    delay_s_ -= tick;
    return;
  }
  delay_s_ = 0;
  for (;;) {
    while (ev_ < page.events.size() && page.events[ev_].at <= visible_) {
      const PageEvent& event = page.events[ev_++];
      switch (event.kind) {
        case PageEvent::Kind::Delay:
          if (!fast_ && event.arg > 0) {
            delay_s_ = static_cast<float>(event.arg) / kScriptFrameHz;
            return;
          }
          break;
        case PageEvent::Kind::Speed: speed_ = event.arg; break;
        case PageEvent::Kind::Wait:
          state_ = State::WaitConfirm;
          fast_ = false;
          return;
        case PageEvent::Kind::FWait:
          if (event_get && event_get() != event.arg) {
            --ev_;
            return;
          }
          break;
        case PageEvent::Kind::AutoClose: auto_close_ = event.arg; break;
        case PageEvent::Kind::FSet:
          if (event_set) event_set(event.arg);
          break;
      }
    }
    if (visible_ >= page.glyphs.size()) {
      fast_ = false;
      if (!page.ends_message) {
        state_ = State::WaitPage;
      } else if (auto_close_ >= 0) {
        state_ = State::Closing;
        close_s_ = static_cast<float>(auto_close_) / kScriptFrameHz;
      } else {
        state_ = State::WaitClose;
      }
      return;
    }
    if (speed_ >= 255 || fast_) {
      ++visible_;
      continue;
    }
    acc_ += static_cast<float>(speed_) / 64.0f;
    while (acc_ >= 1.0f && visible_ < page.glyphs.size()) {
      ++visible_;
      acc_ -= 1.0f;
      if (ev_ < page.events.size() && page.events[ev_].at <= visible_) break;
    }
    return;
  }
}

void Typewriter::confirm() {
  switch (state_) {
    case State::Revealing: fast_ = true; break;
    case State::WaitConfirm:
      state_ = State::Revealing;
      break;
    case State::WaitPage:
      if (page_ + 1 < pages_.size()) {
        ++page_;
        begin_page();
        state_ = State::Revealing;
      } else {
        state_ = State::Closed;
      }
      break;
    case State::WaitClose:
    case State::Closing: state_ = State::Closed; break;
    default: break;
  }
}

}  // namespace oc::ui
