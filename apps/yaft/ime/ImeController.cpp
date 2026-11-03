#include "ImeController.h"

#include "Utf8.h"

namespace ime {

namespace {

using Special = ImeKey::Special;

bool
isPrintable(const ImeKey& key) {
  return key.special == Special::None && key.ch > U' ' && key.ch != 0x7F &&
         !key.ctrl && !key.alt;
}

bool
isCtrlChar(const ImeKey& key, char32_t c) {
  return key.special == Special::None && key.ch == c && key.ctrl && !key.alt;
}

// Emacs-style aliases for the keys composition and conversion react to.
ImeKey
normalize(ImeKey key) {
  if (isCtrlChar(key, U'h')) {
    return ImeKey::named(Special::Backspace);
  }
  if (isCtrlChar(key, U'm') || isCtrlChar(key, U'j')) {
    return ImeKey::named(Special::Enter);
  }
  if (isCtrlChar(key, U'[') || isCtrlChar(key, U'g')) {
    return ImeKey::named(Special::Escape);
  }
  return key;
}

bool
isPlain(const ImeKey& key) {
  return !key.ctrl && !key.alt;
}

} // namespace

ImeController::ImeController(Composer composer,
                             Converter* converter,
                             ImeConfig config)
  : composer(std::move(composer)), converter(converter), config(config) {}

ImeResult
ImeController::handleKey(const ImeKey& key) {
  const auto before = view();
  auto result = dispatchKey(key);
  result.changed = view() != before;
  return result;
}

ImeResult
ImeController::handleCommand(ImeCommand command) {
  const auto before = view();

  ImeResult result;
  const bool enable =
    command == ImeCommand::On || (command == ImeCommand::Toggle && !isEnabled);
  if (enable) {
    isEnabled = true;
  } else if (isEnabled) {
    result.commit = commitAll();
    isEnabled = false;
  }

  result.changed = view() != before;
  return result;
}

ImeResult
ImeController::dispatchKey(const ImeKey& rawKey) {
  waitingForMozc = false;

  // Ctrl+Space toggles from anywhere.
  if (rawKey.special == Special::None && rawKey.ch == U' ' && rawKey.ctrl &&
      !rawKey.alt) {
    return handleCommand(ImeCommand::Toggle);
  }

  if (!isEnabled) {
    return { {}, true, false };
  }

  switch (mode) {
    case Mode::Direct:
      return handleDirect(rawKey);
    case Mode::Composing:
      return handleComposing(normalize(rawKey));
    case Mode::Converting:
      return handleConverting(normalize(rawKey));
  }
  return { {}, true, false };
}

ImeResult
ImeController::handleDirect(const ImeKey& key) {
  if (isPrintable(key)) {
    composer.add(key.ch);
    mode = Mode::Composing;
    return {};
  }

  const bool isEscape = key.special == Special::Escape || isCtrlChar(key, U'[');
  if (isEscape && config.escapeOff) {
    isEnabled = false;
  }
  return { {}, true, false };
}

ImeResult
ImeController::handleComposing(const ImeKey& key) {
  if (isPrintable(key)) {
    composer.add(key.ch);
    return {};
  }

  if (key.special == Special::Backspace && isPlain(key)) {
    composer.backspace();
    if (composer.empty()) {
      mode = Mode::Direct;
    }
    return {};
  }

  if (key.special == Special::Enter && isPlain(key)) {
    return { commitComposition(), false, false };
  }

  if (key.special == Special::Escape && isPlain(key)) {
    composer.clear();
    mode = Mode::Direct;
    return {};
  }

  if (key.special == Special::None && key.ch == U' ' && isPlain(key)) {
    startConversion();
    return {};
  }

  return { commitComposition(), true, false };
}

ImeResult
ImeController::handleConverting(const ImeKey& key) {
  const auto navigate = [this](const MozcKey& mozcKey) {
    sendConversionKey(mozcKey);
    return ImeResult{};
  };

  if (isPlain(key)) {
    if (key.special == Special::None && key.ch == U' ') {
      return navigate(mozcSymbolKey({ "space" }));
    }
    switch (key.special) {
      case Special::Down:
      case Special::Tab:
        return navigate(mozcSymbolKey({ "space" }));
      case Special::Up:
        return navigate(mozcSymbolKey({ "up" }));
      case Special::Left:
        return navigate(key.shift ? mozcSymbolKey({ "shift", "left" })
                                  : mozcSymbolKey({ "left" }));
      case Special::Right:
        return navigate(key.shift ? mozcSymbolKey({ "shift", "right" })
                                  : mozcSymbolKey({ "right" }));
      case Special::Enter: {
        if (const auto text = commitConversion(); text.has_value()) {
          return { *text, false, false };
        }
        fallBackToComposing();
        return {};
      }
      case Special::Escape:
      case Special::Backspace:
        cancelConversion();
        return {};
      default:
        break;
    }

    // Digits only move the candidate focus, like in mozc itself.
    if (key.special == Special::None && key.ch >= U'1' && key.ch <= U'9') {
      return navigate(mozcCodeKey(int(key.ch)));
    }
  }

  if (isPrintable(key)) {
    std::string text;
    if (const auto committed = commitConversion(); committed.has_value()) {
      text = *committed;
      composer.clear();
    } else {
      text = commitComposition();
    }
    composer.add(key.ch);
    mode = Mode::Composing;
    conversion.reset();
    return { text, false, false };
  }

  return { commitAll(), true, false };
}

std::string
ImeController::commitAll() {
  switch (mode) {
    case Mode::Direct:
      return {};
    case Mode::Composing:
      return commitComposition();
    case Mode::Converting:
      if (const auto text = commitConversion(); text.has_value()) {
        return *text;
      }
      return commitComposition();
  }
  return {};
}

std::string
ImeController::commitComposition() {
  auto text = composer.strippedText();
  composer.clear();
  conversion.reset();
  mode = Mode::Direct;
  return text;
}

std::optional<std::string>
ImeController::commitConversion() {
  const auto output = converter == nullptr
                        ? std::nullopt
                        : converter->sendKeys({ mozcSymbolKey({ "enter" }) });
  if (!output.has_value() || !output->result.has_value()) {
    // Don't leave a stale preedit behind in a mozc that is still alive.
    if (output.has_value()) {
      converter->sendKeys(
        { mozcSymbolKey({ "escape" }), mozcSymbolKey({ "escape" }) });
    }
    return std::nullopt;
  }

  composer.clear();
  conversion.reset();
  mode = Mode::Direct;
  return *output->result;
}

void
ImeController::startConversion() {
  const auto text = composer.strippedText();
  if (text.empty()) {
    return;
  }

  if (converter == nullptr || converter->state() != ConverterState::Ready) {
    if (converter != nullptr) {
      converter->restart();
    }
    waitingForMozc = true;
    return;
  }

  std::vector<MozcKey> keys;
  for (const auto c : decodeUtf8(text)) {
    keys.push_back(mozcKanaKey(encodeUtf8(std::u32string(1, c))));
  }
  keys.push_back(mozcSymbolKey({ "space" }));

  const auto output = converter->sendKeys(keys);
  if (!output.has_value() || !output->preedit.has_value()) {
    waitingForMozc = true;
    return;
  }

  conversion = output;
  mode = Mode::Converting;
}

void
ImeController::cancelConversion() {
  // The first escape goes back to the hiragana preedit, the second clears it.
  if (converter != nullptr) {
    converter->sendKeys(
      { mozcSymbolKey({ "escape" }), mozcSymbolKey({ "escape" }) });
  }
  conversion.reset();
  mode = Mode::Composing;
}

bool
ImeController::sendConversionKey(const MozcKey& key) {
  const auto output =
    converter == nullptr ? std::nullopt : converter->sendKeys({ key });
  if (!output.has_value() || !output->preedit.has_value()) {
    fallBackToComposing();
    return false;
  }
  conversion = output;
  return true;
}

void
ImeController::fallBackToComposing() {
  conversion.reset();
  mode = Mode::Composing;
  waitingForMozc = true;
}

ImeView
ImeController::view() {
  ImeView view;
  view.enabled = isEnabled;

  switch (mode) {
    case Mode::Direct:
      view.mode = ImeView::Mode::Direct;
      break;

    case Mode::Composing:
      view.mode = ImeView::Mode::Composing;
      view.preedit.push_back({ composer.text(), false });
      break;

    case Mode::Converting: {
      view.mode = ImeView::Mode::Converting;
      if (!conversion.has_value()) {
        break;
      }

      if (conversion->preedit.has_value()) {
        for (const auto& segment : conversion->preedit->segments) {
          view.preedit.push_back({ segment.value, segment.highlighted });
        }
      }

      if (conversion->candidates.has_value()) {
        const auto& candidates = *conversion->candidates;
        for (const auto& candidate : candidates.items) {
          if (candidate.index == candidates.focusedIndex) {
            view.focused = int(view.candidates.size());
          }
          view.candidates.push_back(
            { candidate.shortcut, candidate.value, candidate.description });
        }
        if (candidates.focusedIndex >= 0 && candidates.size > 0) {
          view.pageInfo = std::to_string(candidates.focusedIndex + 1) + "/" +
                          std::to_string(candidates.size);
        }
      }
      break;
    }
  }

  if (waitingForMozc) {
    const auto state =
      converter == nullptr ? ConverterState::Unavailable : converter->state();
    if (state == ConverterState::Starting) {
      view.status = "mozc 起動中";
    } else if (state == ConverterState::Unavailable) {
      view.status = "mozc 未接続";
    }
  }
  return view;
}

} // namespace ime
