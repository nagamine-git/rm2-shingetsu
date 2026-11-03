#pragma once

#include "ime/Converter.h"

#include <string>
#include <string_view>
#include <vector>

// A shell stand-in for mozc_emacs_helper: prints the first line of the given
// transcript as greeting, then answers each request line with the next one.
constexpr std::string_view mozc_replay_script = R"(exec 3< "$1"
IFS= read -r line <&3 && printf '%s\n' "$line"
while IFS= read -r request; do
  IFS= read -r line <&3 || exit 1
  printf '%s\n' "$line"
done
)";

/// Scriptable stand-in for mozc: remembers what it was sent and answers like
/// a (very) small mozc would, with `num_candidates` candidates per reading.
class FakeConverter : public ime::Converter {
public:
  ime::ConverterState current = ime::ConverterState::Ready;
  bool failNext = false;
  int restarts = 0;
  int num_candidates = 12;

  // One entry per sendKeys() call, each key in the form "97:わ" / "space".
  std::vector<std::vector<std::string>> calls;

  ime::ConverterState state() override { return current; }

  void restart() override { restarts++; }

  std::optional<ime::MozcOutput> sendKeys(
    const std::vector<ime::MozcKey>& keys) override {
    std::vector<std::string> described;
    for (const auto& key : keys) {
      described.push_back(describe(key));
    }
    calls.push_back(described);

    if (current != ime::ConverterState::Ready) {
      return std::nullopt;
    }
    if (failNext) {
      failNext = false;
      current = ime::ConverterState::Unavailable;
      return std::nullopt;
    }

    ime::MozcOutput out;
    for (const auto& key : described) {
      out = apply(key);
    }
    return out;
  }

private:
  static std::string describe(const ime::MozcKey& key) {
    std::string out;
    for (const auto& token : key) {
      if (!out.empty()) {
        out += token.type == ime::MozcKeyToken::Type::String ? ":" : " ";
      }
      out += token.type == ime::MozcKeyToken::Type::Code
               ? std::to_string(token.code)
               : token.text;
    }
    return out;
  }

  std::string candidate(int i) const {
    return i == 0 ? reading : reading + "#" + std::to_string(i);
  }

  ime::MozcOutput preeditOutput() const {
    ime::MozcOutput out;
    out.consumed = true;
    ime::MozcPreedit preedit;
    if (!converting) {
      if (!reading.empty()) {
        preedit.segments.push_back({ reading, false });
      }
    } else {
      preedit.segments.push_back({ candidate(focus), true });
      preedit.highlightedPosition = 0;
    }
    out.preedit = preedit;

    if (converting && spaces >= 2) {
      ime::MozcCandidates cands;
      cands.focusedIndex = focus;
      cands.size = num_candidates;
      cands.pageSize = 9;
      cands.category = "conversion";
      cands.position = focus / 9 * 9;
      for (int i = cands.position;
           i < std::min(num_candidates, cands.position + 9);
           i++) {
        cands.items.push_back({ i,
                                candidate(i),
                                i == 0 ? "ひらがな" : "",
                                std::to_string(i - cands.position + 1) });
      }
      out.candidates = cands;
    }
    return out;
  }

  ime::MozcOutput apply(const std::string& key) {
    const auto colon = key.find(':');
    if (key.rfind("97:", 0) == 0 && colon != std::string::npos) {
      reading += key.substr(colon + 1);
      return preeditOutput();
    }

    if (key == "space") {
      converting = true;
      spaces++;
      focus = spaces >= 2 ? std::min(spaces - 1, num_candidates - 1) : 0;
    } else if (key == "enter") {
      ime::MozcOutput out;
      out.consumed = true;
      if (!reading.empty()) {
        out.result = converting ? candidate(focus) : reading;
      }
      reset();
      return out;
    } else if (key == "escape" || key == "backspace") {
      if (converting) {
        converting = false;
        spaces = 0;
        focus = 0;
      } else {
        reset();
      }
    } else if (key.size() == 2 && key[0] >= '4' && key[0] <= '5') {
      // Digit key codes: '1'..'9' are 49..57. They only move the focus.
      const int digit = std::stoi(key) - 48;
      if (digit >= 1 && digit <= 9) {
        focus = std::min(digit - 1 + focus / 9 * 9, num_candidates - 1);
      }
    }
    return preeditOutput();
  }

  void reset() {
    reading.clear();
    converting = false;
    spaces = 0;
    focus = 0;
  }

  std::string reading;
  bool converting = false;
  int spaces = 0;
  int focus = 0;
};
