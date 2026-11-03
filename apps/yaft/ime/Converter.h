#pragma once

#include "MozcProtocol.h"

#include <optional>
#include <vector>

namespace ime {

enum class ConverterState { Starting, Ready, Unavailable };

/// What the IME controller needs from a kana-kanji converter (mozc).
class Converter {
public:
  virtual ~Converter() = default;

  virtual ConverterState state() = 0;

  /// Tries to bring an unavailable converter back. Implementations rate
  /// limit this, so it is cheap to call on every conversion attempt.
  virtual void restart() = 0;

  /// Sends the keys in order and returns the output of the last one.
  /// std::nullopt means the converter failed (and is now unavailable).
  virtual std::optional<MozcOutput> sendKeys(
    const std::vector<MozcKey>& keys) = 0;
};

} // namespace ime
