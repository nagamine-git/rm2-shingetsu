#include "Sexp.h"

#include <cctype>
#include <cstdlib>

namespace ime {

namespace {

class Parser {
public:
  explicit Parser(std::string_view input) : in(input) {}

  std::optional<Sexp> parseAll(std::string* error) {
    auto value = parseValue();
    skipSpace();
    if (value.has_value() && pos != in.size()) {
      err = "trailing input";
      value.reset();
    }
    if (!value.has_value() && error != nullptr) {
      *error = err + " at offset " + std::to_string(pos);
    }
    return value;
  }

private:
  void skipSpace() {
    while (pos < in.size() &&
           std::isspace(static_cast<unsigned char>(in[pos])) != 0) {
      pos++;
    }
  }

  static bool isDelimiter(char c) {
    return c == '(' || c == ')' || c == '"' ||
           std::isspace(static_cast<unsigned char>(c)) != 0;
  }

  std::optional<Sexp> parseValue() {
    skipSpace();
    if (pos >= in.size()) {
      err = "unexpected end";
      return std::nullopt;
    }

    const char c = in[pos];
    if (c == '(') {
      pos++;
      return parseList();
    }
    if (c == ')') {
      err = "unexpected ')'";
      return std::nullopt;
    }
    if (c == '"') {
      return parseString();
    }
    return parseAtom();
  }

  std::optional<Sexp> parseList() {
    Sexp list;
    while (true) {
      skipSpace();
      if (pos >= in.size()) {
        err = "unterminated list";
        return std::nullopt;
      }
      if (in[pos] == ')') {
        pos++;
        return list;
      }

      // A lone '.' starts the tail of an improper list.
      if (in[pos] == '.' && pos + 1 < in.size() && isDelimiter(in[pos + 1]) &&
          !list.items.empty()) {
        pos++;
        auto tail = parseValue();
        if (!tail.has_value()) {
          return std::nullopt;
        }
        skipSpace();
        if (pos >= in.size() || in[pos] != ')') {
          err = "expected ')' after dotted tail";
          return std::nullopt;
        }
        pos++;

        if (tail->isList()) {
          for (auto& item : tail->items) {
            list.items.push_back(std::move(item));
          }
          list.tail = std::move(tail->tail);
        } else {
          list.tail = std::make_shared<Sexp>(std::move(*tail));
        }
        return list;
      }

      auto item = parseValue();
      if (!item.has_value()) {
        return std::nullopt;
      }
      list.items.push_back(std::move(*item));
    }
  }

  std::optional<Sexp> parseString() {
    pos++; // opening quote
    Sexp str;
    str.type = Sexp::Type::String;
    while (pos < in.size()) {
      const char c = in[pos++];
      if (c == '"') {
        return str;
      }
      if (c == '\\' && pos < in.size()) {
        str.text += in[pos++];
      } else {
        str.text += c;
      }
    }
    err = "unterminated string";
    return std::nullopt;
  }

  std::optional<Sexp> parseAtom() {
    const auto start = pos;
    while (pos < in.size() && !isDelimiter(in[pos])) {
      pos++;
    }
    const std::string token(in.substr(start, pos - start));

    Sexp atom;
    char* end = nullptr;
    const auto number = std::strtoll(token.c_str(), &end, 10);
    if (end != token.c_str() && *end == '\0') {
      atom.type = Sexp::Type::Int;
      atom.number = number;
    } else {
      atom.type = Sexp::Type::Symbol;
      atom.text = token;
    }
    return atom;
  }

  std::string_view in;
  size_t pos = 0;
  std::string err;
};

} // namespace

std::optional<Sexp>
Sexp::parse(std::string_view input, std::string* error) {
  return Parser(input).parseAll(error);
}

const Sexp*
Sexp::find(std::string_view key) const {
  if (!isList()) {
    return nullptr;
  }
  for (const auto& item : items) {
    if (item.isList() && !item.items.empty() &&
        item.items.front().isSymbol(key)) {
      return &item;
    }
  }
  return nullptr;
}

std::string
Sexp::stringOr(std::string def) const {
  if (const auto* value = atom();
      value != nullptr &&
      (value->type == Type::String || value->type == Type::Symbol)) {
    return value->text;
  }
  return def;
}

int64_t
Sexp::intOr(int64_t def) const {
  if (const auto* value = atom();
      value != nullptr && value->type == Type::Int) {
    return value->number;
  }
  return def;
}

bool
Sexp::boolOr(bool def) const {
  if (const auto* value = atom(); value != nullptr) {
    return !value->isNil();
  }
  return def;
}

} // namespace ime
