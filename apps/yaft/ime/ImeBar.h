#pragma once

#include "ImeController.h"

#include <UI.h>

class ImeBarRenderObject;

/// One-line bar between the terminal and the keyboard that shows the input
/// method state: mode, preedit, candidates and status.
class ImeBar : public rmlib::Widget<ImeBarRenderObject> {
public:
  // 1px border + 3px padding + one terminal cell (32px) + 3px padding. Fixed,
  // so toggling the input method never resizes the terminal.
  static constexpr int height = 39;

  /// A hidden bar takes no space, for when the input method is disabled.
  ImeBar(ime::ImeView view, bool visible)
    : view(std::move(view)), visible(visible) {}

  std::unique_ptr<rmlib::RenderObject> createRenderObject() const;

private:
  friend class ImeBarRenderObject;
  ime::ImeView view;
  bool visible;
};

class ImeBarRenderObject : public rmlib::LeafRenderObject<ImeBar> {
public:
  explicit ImeBarRenderObject(const ImeBar& bar)
    : LeafRenderObject(bar), shown(bar.view), visible(bar.visible) {}

  void update(const ImeBar& bar);

protected:
  rmlib::Size doLayout(const rmlib::Constraints& constraints) final;
  void doDraw(rmlib::Canvas& canvas,
              std::vector<rmlib::UpdateRegion>& out) final;

private:
  // Own copy: the widget a render object points to is replaced on rebuild.
  ime::ImeView shown;
  bool visible;
};
