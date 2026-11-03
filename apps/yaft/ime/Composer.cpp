#include "Composer.h"

#include "Utf8.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <vector>

namespace ime {

namespace {

#include "shingetsu-ansi-colemak.tsv.h"
#include "shingetsu-ansi-qwerty.tsv.h"

std::vector<std::string_view>
splitOmittingEmpty(std::string_view line, char sep) {
  std::vector<std::string_view> fields;
  size_t start = 0;
  while (start <= line.size()) {
    auto end = line.find(sep, start);
    if (end == std::string_view::npos) {
      end = line.size();
    }
    if (end > start) {
      fields.push_back(line.substr(start, end - start));
    }
    start = end + 1;
  }
  return fields;
}

// Like azooKey's parseKey/parseValue. Other {...} tokens (wildcards, shift
// keys) can't be represented here, so those lines are dropped.
std::optional<std::u32string>
parseField(std::string_view field) {
  const auto str = decodeUtf8(field);
  std::u32string out;
  for (size_t i = 0; i < str.size(); i++) {
    if (str[i] == U'{') {
      const auto end = str.find(U'}', i);
      if (end != std::u32string::npos) {
        const auto token = encodeUtf8(str.substr(i + 1, end - i - 1));
        if (token == "lbracket") {
          out += U'{';
        } else if (token == "rbracket") {
          out += U'}';
        } else {
          return std::nullopt;
        }
        i = end;
        continue;
      }
    }
    out += str[i];
  }
  return out;
}

bool
isTextChar(char32_t c) {
  // Kana, kanji and ASCII alphanumerics always stay in the output.
  return (c >= 0x3000 && c <= 0x30FF) || (c >= 0x4E00 && c <= 0x9FFF) ||
         (c < 0x80 && ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
                       (c >= 'a' && c <= 'z')));
}

} // namespace

Composer
Composer::fromTsv(std::string_view tsv) {
  Composer composer;

  size_t pos = 0;
  while (pos <= tsv.size()) {
    auto end = tsv.find('\n', pos);
    if (end == std::string_view::npos) {
      end = tsv.size();
    }
    auto line = tsv.substr(pos, end - pos);
    pos = end + 1;

    if (!line.empty() && line.back() == '\r') {
      line.remove_suffix(1);
    }
    if (line.find_first_not_of(" \t") == std::string_view::npos) {
      continue;
    }

    const auto fields = splitOmittingEmpty(line, '\t');
    if (fields.size() < 2) {
      continue;
    }

    const auto key = parseField(fields[0]);
    const auto value = parseField(fields[1]);
    if (!key.has_value() || !value.has_value() || key->empty()) {
      continue;
    }

    // Later lines override earlier ones.
    composer.table[*key] = *value;
  }

  // "ee" can't match as written, the buffer holds the first e's placeholder
  // by then. So also register "<placeholder>e".
  std::unordered_set<char32_t> firstChars;
  for (const auto& [key, value] : composer.table) {
    if (key.size() >= 2) {
      firstChars.insert(key.front());
    }
  }

  std::vector<std::pair<std::u32string, std::u32string>> extra;
  for (const auto& [key, value] : composer.table) {
    if (key.size() < 2) {
      continue;
    }
    const auto first = composer.table.find(key.substr(0, 1));
    if (first == composer.table.end() || first->second.size() != 1) {
      continue;
    }
    const auto replacement = first->second.front();
    if (replacement != key.front() && firstChars.count(replacement) != 0) {
      extra.emplace_back(first->second + key.substr(1), value);
    }
  }
  for (auto& [key, value] : extra) {
    composer.table.emplace(std::move(key), std::move(value));
  }

  // Placeholders: single-key outputs that start other keys, are never
  // typed mid-key, and aren't text (unlike e.g. か, which starts "かi").
  std::unordered_set<char32_t> laterChars;
  for (const auto& [key, value] : composer.table) {
    laterChars.insert(key.begin() + 1, key.end());
    composer.maxKeyLength = std::max(composer.maxKeyLength, key.size());
  }
  for (const auto& [key, value] : composer.table) {
    if (key.size() == 1 && value.size() == 1 &&
        firstChars.count(value.front()) != 0 &&
        laterChars.count(value.front()) == 0 && !isTextChar(value.front())) {
      composer.placeholders.insert(value.front());
    }
  }

  return composer;
}

std::optional<Composer>
Composer::load(std::string_view nameOrPath, std::string* error) {
  if (nameOrPath == "colemak") {
    return fromTsv(std::string_view(
      reinterpret_cast<const char*>(shingetsu_ansi_colemak_tsv),
      shingetsu_ansi_colemak_tsv_len));
  }
  if (nameOrPath == "qwerty") {
    return fromTsv(
      std::string_view(reinterpret_cast<const char*>(shingetsu_ansi_qwerty_tsv),
                       shingetsu_ansi_qwerty_tsv_len));
  }

  std::ifstream ifs{ std::string(nameOrPath), std::ios::binary };
  if (!ifs) {
    if (error != nullptr) {
      *error = "cannot read table: " + std::string(nameOrPath);
    }
    return std::nullopt;
  }
  std::stringstream content;
  content << ifs.rdbuf();
  return fromTsv(content.str());
}

void
Composer::add(char32_t c) {
  const auto maxPrefix =
    maxKeyLength == 0 ? 0 : std::min(maxKeyLength - 1, buffer.size());
  for (size_t prefix = maxPrefix + 1; prefix-- > 0;) {
    auto key = buffer.substr(buffer.size() - prefix);
    key += c;

    const auto it = table.find(key);
    if (it != table.end()) {
      buffer.resize(buffer.size() - prefix);
      buffer += it->second;
      return;
    }
  }
  buffer += c;
}

bool
Composer::backspace() {
  if (buffer.empty()) {
    return false;
  }
  buffer.pop_back();
  return true;
}

std::string
Composer::text() const {
  return encodeUtf8(buffer);
}

std::string
Composer::strippedText() const {
  std::string out;
  for (const auto c : buffer) {
    if (placeholders.count(c) == 0) {
      appendUtf8(out, c);
    }
  }
  return out;
}

} // namespace ime
