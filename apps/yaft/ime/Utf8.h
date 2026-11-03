#pragma once

#include <string>
#include <string_view>

namespace ime {

inline void
appendUtf8(std::string& out, char32_t c) {
  if (c < 0x80) {
    out += char(c);
  } else if (c < 0x800) {
    out += char(0xC0 | (c >> 6));
    out += char(0x80 | (c & 0x3F));
  } else if (c < 0x10000) {
    out += char(0xE0 | (c >> 12));
    out += char(0x80 | ((c >> 6) & 0x3F));
    out += char(0x80 | (c & 0x3F));
  } else {
    out += char(0xF0 | (c >> 18));
    out += char(0x80 | ((c >> 12) & 0x3F));
    out += char(0x80 | ((c >> 6) & 0x3F));
    out += char(0x80 | (c & 0x3F));
  }
}

inline std::string
encodeUtf8(std::u32string_view str) {
  std::string out;
  for (const auto c : str) {
    appendUtf8(out, c);
  }
  return out;
}

/// Lenient decoder: every malformed byte becomes U+FFFD.
inline std::u32string
decodeUtf8(std::string_view str) {
  std::u32string out;
  for (size_t i = 0; i < str.size();) {
    const auto lead = static_cast<unsigned char>(str[i]);
    int extra = 0;
    char32_t code = 0;
    if (lead < 0x80) {
      code = lead;
    } else if ((lead & 0xE0) == 0xC0) {
      extra = 1;
      code = lead & 0x1F;
    } else if ((lead & 0xF0) == 0xE0) {
      extra = 2;
      code = lead & 0x0F;
    } else if ((lead & 0xF8) == 0xF0) {
      extra = 3;
      code = lead & 0x07;
    } else {
      out += U'�';
      i++;
      continue;
    }

    bool valid = i + extra < str.size();
    for (int j = 1; valid && j <= extra; j++) {
      const auto cont = static_cast<unsigned char>(str[i + j]);
      if ((cont & 0xC0) != 0x80) {
        valid = false;
        break;
      }
      code = (code << 6) | (cont & 0x3F);
    }

    if (!valid) {
      out += U'�';
      i++;
      continue;
    }
    out += code;
    i += extra + 1;
  }
  return out;
}

} // namespace ime
