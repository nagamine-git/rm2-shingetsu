#pragma once

#include "Composer.h"
#include "Converter.h"

#include <optional>
#include <string>
#include <vector>

namespace ime {

struct ImeKey {
  enum class Special {
    None,
    Enter,
    Backspace,
    Escape,
    Tab,
    Left,
    Right,
    Up,
    Down,
    Home,
    End,
    PageUp,
    PageDown,
    Delete,
  };

  Special special = Special::None;
  char32_t ch = 0; // Printable character (including ' ') if special == None.
  bool shift = false;
  bool ctrl = false;
  bool alt = false;

  static ImeKey character(char32_t c, bool ctrl = false, bool alt = false) {
    return { Special::None, c, false, ctrl, alt };
  }
  static ImeKey named(Special special,
                      bool shift = false,
                      bool ctrl = false,
                      bool alt = false) {
    return { special, 0, shift, ctrl, alt };
  }
};

enum class ImeCommand { On, Off, Toggle };

struct ImeResult {
  /// UTF-8 text to type into the terminal. Sanitized (untrusted mozc text):
  /// no control characters, valid UTF-8, length capped.
  std::string commit;
  /// The key was not consumed; send it on to the terminal (after `commit`).
  bool passthrough = false;
  /// What the IME bar shows differs from before.
  bool changed = false;
};

/// Everything the IME bar needs to draw.
struct ImeView {
  enum class Mode { Direct, Composing, Converting };

  struct Segment {
    std::string text;
    bool highlighted = false;
    bool operator==(const Segment& o) const {
      return text == o.text && highlighted == o.highlighted;
    }
  };

  struct Candidate {
    std::string label; // Shortcut digit.
    std::string value;
    std::string description;
    bool operator==(const Candidate& o) const {
      return label == o.label && value == o.value &&
             description == o.description;
    }
  };

  bool enabled = false;
  Mode mode = Mode::Direct;
  std::vector<Segment> preedit;
  std::vector<Candidate> candidates; // Current page only.
  int focused = -1;                  // Index into candidates.
  std::string pageInfo;              // "focused+1/size".
  std::string status;

  bool operator==(const ImeView& o) const {
    return enabled == o.enabled && mode == o.mode && preedit == o.preedit &&
           candidates == o.candidates && focused == o.focused &&
           pageInfo == o.pageInfo && status == o.status;
  }
  bool operator!=(const ImeView& o) const { return !(*this == o); }
};

struct ImeConfig {
  // Escape in direct mode switches the IME off (vim insert -> normal).
  bool escapeOff = true;
};

/// The input method state machine. Composition is local (Composer), the
/// Converter (mozc) is only used from Space until commit or cancel.
class ImeController {
public:
  ImeController(Composer composer, Converter* converter, ImeConfig config = {});

  ImeResult handleKey(const ImeKey& key);
  ImeResult handleCommand(ImeCommand command);

  ImeView view();

  bool enabled() const { return isEnabled; }

private:
  enum class Mode { Direct, Composing, Converting };

  ImeResult dispatchKey(const ImeKey& key);
  ImeResult handleDirect(const ImeKey& key);
  ImeResult handleComposing(const ImeKey& key);
  ImeResult handleConverting(const ImeKey& key);

  /// Commits whatever is being composed/converted and goes back to Direct.
  std::string commitAll();
  std::string commitComposition();
  /// Enter in mozc; the committed text or std::nullopt if mozc failed.
  std::optional<std::string> commitConversion();

  void startConversion();
  void cancelConversion();
  // Sends one key during conversion; false if mozc failed.
  bool sendConversionKey(const MozcKey& key);
  void fallBackToComposing();

  Composer composer;
  Converter* converter;
  ImeConfig config;

  bool isEnabled = false;
  Mode mode = Mode::Direct;
  std::optional<MozcOutput> conversion;
  bool waitingForMozc = false;
};

} // namespace ime
