#include "MozcProtocol.h"

namespace ime {

namespace {

MozcPreedit
parsePreedit(const Sexp& entry) {
  MozcPreedit preedit;
  if (const auto* cursor = entry.find("cursor"); cursor != nullptr) {
    preedit.cursor = int(cursor->intOr(0));
  }
  if (const auto* pos = entry.find("highlighted-position"); pos != nullptr) {
    preedit.highlightedPosition = int(pos->intOr(-1));
  }
  if (const auto* segments = entry.find("segment"); segments != nullptr) {
    for (auto it = segments->restBegin(); it != segments->restEnd(); ++it) {
      MozcSegment segment;
      if (const auto* value = it->find("value"); value != nullptr) {
        segment.value = value->stringOr();
      }
      if (const auto* annotation = it->find("annotation");
          annotation != nullptr) {
        segment.highlighted = annotation->stringOr() == "highlight";
      }
      preedit.segments.push_back(std::move(segment));
    }
  }
  return preedit;
}

MozcCandidates
parseCandidates(const Sexp& entry) {
  MozcCandidates candidates;
  if (const auto* focused = entry.find("focused-index"); focused != nullptr) {
    candidates.focusedIndex = int(focused->intOr(-1));
  }
  if (const auto* size = entry.find("size"); size != nullptr) {
    candidates.size = int(size->intOr(0));
  }
  if (const auto* position = entry.find("position"); position != nullptr) {
    candidates.position = int(position->intOr(0));
  }
  if (const auto* pageSize = entry.find("page-size"); pageSize != nullptr) {
    candidates.pageSize = int(pageSize->intOr(0));
  }
  if (const auto* category = entry.find("category"); category != nullptr) {
    candidates.category = category->stringOr();
  }

  if (const auto* items = entry.find("candidate"); items != nullptr) {
    for (auto it = items->restBegin(); it != items->restEnd(); ++it) {
      MozcCandidate candidate;
      if (const auto* index = it->find("index"); index != nullptr) {
        candidate.index = int(index->intOr(0));
      }
      if (const auto* value = it->find("value"); value != nullptr) {
        candidate.value = value->stringOr();
      }
      if (const auto* annotation = it->find("annotation");
          annotation != nullptr) {
        if (const auto* desc = annotation->find("description");
            desc != nullptr) {
          candidate.description = desc->stringOr();
        }
        if (const auto* shortcut = annotation->find("shortcut");
            shortcut != nullptr) {
          candidate.shortcut = shortcut->stringOr();
        }
      }
      candidates.items.push_back(std::move(candidate));
    }
  }
  return candidates;
}

} // namespace

std::string
MozcPreedit::text() const {
  std::string out;
  for (const auto& segment : segments) {
    out += segment.value;
  }
  return out;
}

MozcOutput
parseMozcOutput(const Sexp& output) {
  MozcOutput result;
  if (const auto* consumed = output.find("consumed"); consumed != nullptr) {
    result.consumed = consumed->boolOr(false);
  }
  if (const auto* preedit = output.find("preedit"); preedit != nullptr) {
    result.preedit = parsePreedit(*preedit);
  }
  if (const auto* candidates = output.find("candidates");
      candidates != nullptr) {
    result.candidates = parseCandidates(*candidates);
  }
  if (const auto* res = output.find("result"); res != nullptr) {
    if (const auto* value = res->find("value"); value != nullptr) {
      result.result = value->stringOr();
    }
  }
  return result;
}

std::optional<MozcMessage>
parseMozcMessage(std::string_view line, std::string* error) {
  const auto sexp = Sexp::parse(line, error);
  if (!sexp.has_value()) {
    return std::nullopt;
  }

  MozcMessage msg;
  if (const auto* version = sexp->find("mozc-emacs-helper");
      version != nullptr) {
    msg.kind = MozcMessage::Kind::Greeting;
    if (const auto* ver = sexp->find("version"); ver != nullptr) {
      msg.version = ver->stringOr();
    }
    return msg;
  }

  if (const auto* err = sexp->find("error"); err != nullptr) {
    msg.kind = MozcMessage::Kind::Error;
    msg.error.type = err->stringOr();
    if (const auto* text = sexp->find("message"); text != nullptr) {
      msg.error.message = text->stringOr();
    }
    return msg;
  }

  const auto* eventId = sexp->find("emacs-event-id");
  if (eventId == nullptr) {
    if (error != nullptr) {
      *error = "not a helper message";
    }
    return std::nullopt;
  }

  msg.kind = MozcMessage::Kind::Reply;
  msg.eventId = eventId->intOr(-1);
  if (const auto* session = sexp->find("emacs-session-id");
      session != nullptr) {
    msg.sessionId = session->intOr(-1);
  }
  if (const auto* output = sexp->find("output"); output != nullptr) {
    msg.output = parseMozcOutput(*output);
  }
  return msg;
}

MozcKey
mozcCodeKey(int code) {
  return { { MozcKeyToken::Type::Code, code, {} } };
}

MozcKey
mozcSymbolKey(std::initializer_list<std::string_view> symbols) {
  MozcKey key;
  for (const auto symbol : symbols) {
    key.push_back({ MozcKeyToken::Type::Symbol, 0, std::string(symbol) });
  }
  return key;
}

MozcKey
mozcKanaKey(std::string_view kana) {
  return { { MozcKeyToken::Type::Code, 97, {} },
           { MozcKeyToken::Type::String, 0, std::string(kana) } };
}

std::string
quoteSexpString(std::string_view text) {
  std::string out = "\"";
  for (const char c : text) {
    if (c == '\\' || c == '"') {
      out += '\\';
    }
    out += c;
  }
  out += '"';
  return out;
}

std::string
formatCreateSession(int64_t id) {
  return "(" + std::to_string(id) + " CreateSession)\n";
}

std::string
formatSendKey(int64_t id, int64_t session, const MozcKey& key) {
  std::string out =
    "(" + std::to_string(id) + " SendKey " + std::to_string(session);
  for (const auto& token : key) {
    out += ' ';
    switch (token.type) {
      case MozcKeyToken::Type::Code:
        out += std::to_string(token.code);
        break;
      case MozcKeyToken::Type::Symbol:
        out += token.text;
        break;
      case MozcKeyToken::Type::String:
        out += quoteSexpString(token.text);
        break;
    }
  }
  out += ")\n";
  return out;
}

std::string
formatDeleteSession(int64_t id, int64_t session) {
  return "(" + std::to_string(id) + " DeleteSession " +
         std::to_string(session) + ")\n";
}

} // namespace ime
