#include "ImeBar.h"

#include "Utf8.h"

#include "conf.h"
#include "terminal_adapter.h"

#include <algorithm>
#include <string>
#include <vector>

using namespace rmlib;

namespace {

static_assert(ImeBar::height == 1 + 3 + CELL_HEIGHT + 3);

constexpr int border = 1;
constexpr int padding = 3;
// Rows covered by highlighted boxes: everything below the border.
constexpr int box_top = border + 1;
constexpr int box_bottom = ImeBar::height - 1;
constexpr int margin = 4;
constexpr int mode_box_width = 2 * CELL_WIDTH + 4;
constexpr int item_padding = 4;
constexpr int item_gap = 8;
constexpr int section_gap = 16;

const glyph_t*
glyphFor(char32_t code) {
  if (const auto* glyph = lookupGlyph(code); glyph != nullptr) {
    return glyph;
  }
  return lookupGlyph(SUBSTITUTE_WIDE);
}

int
textWidth(const std::u32string& text) {
  int width = 0;
  for (const auto code : text) {
    width += glyphFor(code)->width * CELL_WIDTH;
  }
  return width;
}

int
textWidth(std::string_view text) {
  return textWidth(ime::decodeUtf8(text));
}

class Painter {
public:
  Painter(Canvas& canvas, int top) : canvas(canvas), top(top) {}

  void fill(int x, int width, int color) {
    const auto right = std::min(x + width, canvas.width());
    if (right <= x) {
      return;
    }
    canvas.set(Rect{ { x, box_top }, { right - 1, box_bottom } }, color);
  }

  // Draws one text run in terminal cells; returns the x after it.
  int text(int x, std::string_view str, bool inverted, bool underline) {
    const int ink = inverted ? white : black;
    const int paper = inverted ? black : white;

    for (const auto code : ime::decodeUtf8(str)) {
      const auto* glyph = glyphFor(code);
      const int width = glyph->width * CELL_WIDTH;
      if (x + width > canvas.width()) {
        break;
      }

      for (int row = 0; row < CELL_HEIGHT; row++) {
        const bool underlined = underline && row >= CELL_HEIGHT - 2;
        for (int col = 0; col < width; col++) {
          const bool set =
            underlined ||
            ((glyph->bitmap[row] >> (width - 1 - col)) & 1U) != 0U;
          canvas.setPixel({ x + col, top + row }, set ? ink : paper);
        }
      }
      x += width;
    }
    return x;
  }

private:
  Canvas& canvas;
  int top;
};

void
paint(Canvas& canvas, const ime::ImeView& view) {
  const int top = border + padding;
  const int width = canvas.width();
  Painter painter(canvas, top);

  canvas.set(canvas.rect(), white);
  canvas.drawLine({ 0, 0 }, { width - 1, 0 }, black);

  // Mode box: inverted "あ" when Japanese input is on.
  int x = margin;
  if (view.enabled) {
    painter.fill(x, mode_box_width, black);
    painter.text(x + 2, "あ", /* inverted */ true, false);
  } else {
    canvas.drawRectangle(
      { x, box_top }, { x + mode_box_width - 1, box_bottom }, black);
    painter.text(x + (mode_box_width - CELL_WIDTH) / 2, "A", false, false);
  }
  x += mode_box_width + section_gap;

  // Reserve the right edge for the status and page info.
  int right = width - margin;
  const auto putRight = [&](const std::string& str) {
    if (str.empty()) {
      return;
    }
    right -= textWidth(str);
    painter.text(right, str, false, false);
    right -= section_gap;
  };
  putRight(view.status);
  putRight(view.pageInfo);

  // A preedit too long for the bar shows its tail, where the typing happens.
  struct Run {
    std::u32string text;
    bool highlighted;
  };
  std::vector<Run> runs;
  int total = 0;
  for (const auto& segment : view.preedit) {
    runs.push_back({ ime::decodeUtf8(segment.text), segment.highlighted });
    total += textWidth(runs.back().text);
  }
  for (auto& run : runs) {
    while (total > right - x && !run.text.empty()) {
      total -= glyphFor(run.text.front())->width * CELL_WIDTH;
      run.text.erase(0, 1);
    }
  }
  for (const auto& run : runs) {
    x = painter.text(
      x, ime::encodeUtf8(run.text), run.highlighted, !run.highlighted);
  }

  if (view.candidates.empty()) {
    return;
  }
  x += section_gap;

  // Scroll so the focused candidate stays visible.
  std::vector<std::string> labels;
  std::vector<int> widths;
  for (const auto& candidate : view.candidates) {
    labels.push_back(candidate.label + " " + candidate.value);
    widths.push_back(textWidth(labels.back()) + 2 * item_padding);
  }

  const int available = right - x;
  size_t first = 0;
  if (view.focused >= 0) {
    int used = 0;
    for (size_t i = first; i <= size_t(view.focused); i++) {
      used += widths[i] + item_gap;
    }
    while (first < size_t(view.focused) && used > available) {
      used -= widths[first] + item_gap;
      first++;
    }
  }

  for (size_t i = first; i < labels.size(); i++) {
    if (x + widths[i] > right) {
      break;
    }
    const bool focused = int(i) == view.focused;
    if (focused) {
      painter.fill(x, widths[i], black);
    }
    painter.text(x + item_padding, labels[i], focused, false);
    x += widths[i] + item_gap;
  }
}

} // namespace

std::unique_ptr<RenderObject>
ImeBar::createRenderObject() const {
  return std::make_unique<ImeBarRenderObject>(*this);
}

void
ImeBarRenderObject::update(const ImeBar& bar) {
  if (bar.visible != visible) {
    visible = bar.visible;
    markNeedsLayout();
    markNeedsDraw();
  }
  if (bar.view != shown) {
    shown = bar.view;
    markNeedsDraw(/* full */ false);
  }
  widget = &bar;
}

Size
ImeBarRenderObject::doLayout(const Constraints& constraints) {
  const auto width = constraints.hasBoundedWidth() ? constraints.max.width
                                                   : constraints.min.width;
  const auto height = std::clamp(visible ? ImeBar::height : 0,
                                 constraints.min.height,
                                 constraints.max.height);
  return { width, height };
}

void
ImeBarRenderObject::doDraw(Canvas& canvas, std::vector<UpdateRegion>& out) {
  if (!visible) {
    return;
  }
  paint(canvas, shown);

  // Fast waveform like the keyboard keys, a proper refresh only on full draws.
  if (isPartialDraw()) {
    out.push_back(
      { canvas.rect(), fb::Waveform::DU, fb::UpdateFlags::FastDraw });
  } else {
    out.push_back({ canvas.rect(), fb::Waveform::GC16Fast });
  }
}
