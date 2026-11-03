#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace ime {

/// Composer driven by a hazkey (azooKey) table: like InputTable.apply, a new
/// char replaces the longest key (buffer suffix + char) with its value.
class Composer {
public:
  /// Parses a hazkey TSV the way azooKey's InputStyleManager.loadTable does.
  static Composer fromTsv(std::string_view tsv);

  /// "colemak" and "qwerty" are the embedded tables, anything else is read
  /// as a path to a TSV file.
  static std::optional<Composer> load(std::string_view nameOrPath,
                                      std::string* error = nullptr);

  void add(char32_t c);

  /// Removes the last composed character.
  bool backspace();

  void clear() { buffer.clear(); }
  bool empty() const { return buffer.empty(); }

  /// Composition as typed, including prefix placeholders.
  std::string text() const;

  /// Composition without prefix placeholders (what gets converted/committed).
  std::string strippedText() const;

  size_t tableSize() const { return table.size(); }

private:
  std::unordered_map<std::u32string, std::u32string> table;
  // Characters that only ever act as the first key char (like the shift
  // marks in the shingetsu layout) and never form text on their own.
  std::unordered_set<char32_t> placeholders;
  size_t maxKeyLength = 0;

  std::u32string buffer;
};

} // namespace ime
