#pragma once

#include "Sexp.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ime {

// Plain structs for what mozc_emacs_helper answers.

struct MozcSegment {
  std::string value;
  bool highlighted = false;
};

struct MozcPreedit {
  int cursor = 0;
  std::vector<MozcSegment> segments;
  int highlightedPosition = -1;

  std::string text() const;
};

struct MozcCandidate {
  int index = 0;
  std::string value;
  std::string description;
  std::string shortcut;
};

struct MozcCandidates {
  int focusedIndex = -1;
  int size = 0;
  int position = 0;
  int pageSize = 0;
  std::string category;
  std::vector<MozcCandidate> items; // Only the current page.
};

struct MozcOutput {
  bool consumed = false;
  std::optional<MozcPreedit> preedit;
  std::optional<MozcCandidates> candidates;
  std::optional<std::string> result; // Committed text.
};

struct MozcError {
  std::string type;
  std::string message;
};

struct MozcMessage {
  enum class Kind { Greeting, Reply, Error };

  Kind kind = Kind::Reply;
  std::string version;
  int64_t eventId = -1;
  int64_t sessionId = -1;
  MozcOutput output;
  MozcError error;
};

MozcOutput
parseMozcOutput(const Sexp& output);

/// Parses one line of helper output (greeting, reply or error).
std::optional<MozcMessage>
parseMozcMessage(std::string_view line, std::string* error = nullptr);

// Requests. A key is a list of tokens, e.g. `97 "わ"` or `shift left`.

struct MozcKeyToken {
  enum class Type { Code, Symbol, String };
  Type type;
  int code = 0;
  std::string text;
};

using MozcKey = std::vector<MozcKeyToken>;

MozcKey
mozcCodeKey(int code);

MozcKey
mozcSymbolKey(std::initializer_list<std::string_view> symbols);

/// A kana typed directly: ASCII code 97 plus the kana as key string.
MozcKey
mozcKanaKey(std::string_view kana);

std::string
quoteSexpString(std::string_view text);

std::string
formatCreateSession(int64_t id);

std::string
formatSendKey(int64_t id, int64_t session, const MozcKey& key);

std::string
formatDeleteSession(int64_t id, int64_t session);

} // namespace ime
