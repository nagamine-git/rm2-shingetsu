#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ime {

/// Minimal S-expression reader for mozc_emacs_helper's output. A dotted tail
/// that is a list is spliced into its parent, as in Lisp: (a . (b)) == (a b).
struct Sexp {
  enum class Type { List, Symbol, Int, String };

  Type type = Type::List;
  std::string text;           // Symbol name or String contents.
  int64_t number = 0;         // Int.
  std::vector<Sexp> items;    // List elements.
  std::shared_ptr<Sexp> tail; // Atom after the dot of an improper list.

  static std::optional<Sexp> parse(std::string_view input,
                                   std::string* error = nullptr);

  bool isList() const { return type == Type::List; }
  bool isSymbol(std::string_view name) const {
    return type == Type::Symbol && text == name;
  }
  /// The empty list and the symbol `nil` are both false.
  bool isNil() const {
    return (isList() && items.empty() && tail == nullptr) || isSymbol("nil");
  }

  /// Alist lookup: the entry `(key ...)` of this list, if any.
  const Sexp* find(std::string_view key) const;

  /// Elements after the key of an alist entry.
  const Sexp* restBegin() const {
    return items.data() + (items.empty() ? 0 : 1);
  }
  const Sexp* restEnd() const { return items.data() + items.size(); }
  size_t restSize() const { return items.empty() ? 0 : items.size() - 1; }

  /// Value of `(key . atom)`, nullptr if this entry holds a list instead.
  const Sexp* atom() const { return tail.get(); }

  // Typed accessors for an alist entry, falling back to `def`.
  std::string stringOr(std::string def = {}) const;
  int64_t intOr(int64_t def) const;
  bool boolOr(bool def) const;
};

} // namespace ime
