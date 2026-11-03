#include "config.h"
#include "Error.h"

#include <algorithm>
#include <filesystem>
#include <toml++/toml.h>

namespace {
constexpr std::string_view default_config = R"(
# Layout of the virtual keyboard: "colemak" or "qwerty".
layout = "colemak"

# Keymap for any physical keyboards. Defaults to the us rM pogo keyboard.
#  * rm-colemak, rm-qwerty: the rM Type Folio
#  * colemak, qwerty: a USB keyboard
#  * timower
keymap = "rm-colemak"

# Auto rotate if keyboard is connected.
auto-rotate = true

# Orientation if no keyboard is connected:
#  * none, clockwise, inverted, counterclockwise
rotation = "none"

# Do a full refresh after 1024 updates.
# Set to 0 to disable auto refresh.
auto-refresh = 1024

# Repeat delay for keyboards.
repeat-delay = 600
# Repeat rate in chars per second.
repeat-rate = 25

# Japanese input: Shingetsu layout composition + mozc conversion.
# Ctrl+Space toggles it, and so do the Alt keys of the rm-colemak keymap
# (left tap = off, right tap = on). Space converts, Enter commits.
[ime]
# Show the input bar and let the input method handle keys.
enabled = true

# Kana table: "colemak", "qwerty" or the path of a hazkey-style TSV.
table = "colemak"

# Escape (in direct mode) switches the input method off, handy for vim.
escape-off = true

# Directory of the mozc chroot, and the uid mozc runs as.
mozc-root = "/home/root/.local/share/rm2-shingetsu/mozc-root"
mozc-uid = 1000

# If not empty, run this command instead of the chroot, e.g.
# ["ssh", "host", "mozc_emacs_helper"].
mozc-command = []
)";

void
addError(std::optional<YaftConfigError>& error, YaftConfigError err) {
  if (!error.has_value()) {
    error = err;
    return;
  }
  error->msg += "\r\n" + err.msg;
}

template<typename Value>
void
parseValue(std::optional<YaftConfigError>& error,
           Value& value,
           toml::table& tbl,
           std::string_view name) {
  value = tbl[name].value_or(value);
  tbl.erase(name);
}

template<typename Value>
void
parseMapping(
  std::optional<YaftConfigError>& error,
  Value& value,
  toml::table& tbl,
  std::string_view name,
  std::initializer_list<std::pair<std::string_view, Value>> mapping) {

  auto entry = tbl[name].value<std::string_view>();
  if (!entry.has_value()) {
    return;
  }

  auto it = std::find_if(mapping.begin(), mapping.end(), [&](const auto& pair) {
    return pair.first == *entry;
  });

  if (it == mapping.end()) {
    addError(
      error,
      {
        YaftConfigError::Syntax,
        "error: Invalid " + std::string(name) + ": " + std::string(*entry),
      });
    tbl.erase(name);
    return;
  }

  value = it->second;
  tbl.erase(name);
}

void
parseIme(std::optional<YaftConfigError>& errors,
         ImeSettings& ime,
         toml::table& tbl) {
  const auto* node = tbl.get("ime");
  if (node == nullptr) {
    return;
  }

  auto* imeTbl = tbl["ime"].as_table();
  if (imeTbl == nullptr) {
    addError(errors, { YaftConfigError::Syntax, "error: ime must be a table" });
    tbl.erase("ime");
    return;
  }

  parseValue(errors, ime.enabled, *imeTbl, "enabled");
  parseValue(errors, ime.table, *imeTbl, "table");
  parseValue(errors, ime.escapeOff, *imeTbl, "escape-off");
  parseValue(errors, ime.mozcRoot, *imeTbl, "mozc-root");
  parseValue(errors, ime.mozcUid, *imeTbl, "mozc-uid");

  if (const auto* command = (*imeTbl)["mozc-command"].as_array();
      command != nullptr) {
    ime.mozcCommand.clear();
    for (const auto& arg : *command) {
      if (const auto str = arg.value<std::string>(); str.has_value()) {
        ime.mozcCommand.push_back(*str);
      } else {
        addError(errors,
                 { YaftConfigError::Syntax,
                   "error: ime.mozc-command must be a list of strings" });
        ime.mozcCommand.clear();
        break;
      }
    }
    imeTbl->erase("mozc-command");
  }

  for (const auto& [key, _] : *imeTbl) {
    addError(errors,
             { YaftConfigError::Syntax,
               "warning: Unknown key: ime." + std::string(key) });
  }
  tbl.erase("ime");
}

YaftConfigAndError
getConfig(const toml::table& input) {
  auto tbl = input;

  std::optional<YaftConfigError> errors;
  YaftConfig cfg;

  parseMapping(errors, cfg.layout, tbl, "layout", layouts);
  parseMapping(errors, cfg.keymap, tbl, "keymap", keymaps);
  parseMapping(errors,
               cfg.rotation,
               tbl,
               "rotation",
               { { "none", rmlib::Rotation::None },
                 { "clockwise", rmlib::Rotation::Clockwise },
                 { "inverted", rmlib::Rotation::Inverted },
                 { "counterclockwise", rmlib::Rotation::CounterClockwise } });

  parseValue(errors, cfg.autoRotate, tbl, "auto-rotate");
  parseValue(errors, cfg.autoRefresh, tbl, "auto-refresh");
  parseValue(errors, cfg.repeatDelay, tbl, "repeat-delay");
  parseValue(errors, cfg.repeatRate, tbl, "repeat-rate");
  parseIme(errors, cfg.ime, tbl);

  if (!tbl.empty()) {
    for (const auto& [key, _] : tbl) {
      addError(errors,
               { YaftConfigError::Syntax,
                 "warning: Unknown key: " + std::string(key) });
    }
  }

  return { cfg, errors };
}

} // namespace

std::filesystem::path
getConfigPath() {
  const auto configDir = [] {
    if (const auto* xdgCfg = getenv("XDG_CONFIG_HOME");
        xdgCfg != nullptr && xdgCfg[0] != 0) {
      return std::filesystem::path(xdgCfg);
    }

    const auto* home = getenv("HOME");
    if (home == nullptr || home[0] == 0) {
      home = "/home/root";
    }

    return std::filesystem::path(home) / ".config" / "yaft";
  }();

  return configDir / "config.toml";
}

YaftConfig
YaftConfig::getDefault() {
  auto tbl = toml::parse(default_config);
  return getConfig(tbl).config;
}

YaftConfigAndError
loadConfig(const std::filesystem::path& path) {
  toml::table tbl;

  if (!std::filesystem::exists(path)) {
    return { YaftConfig::getDefault(),
             YaftConfigError{ YaftConfigError::Missing, path.string() } };
  }

  try {
    tbl = toml::parse_file(path.c_str());
  } catch (const toml::parse_error& err) {
    return { YaftConfig::getDefault(),
             YaftConfigError{ YaftConfigError::Syntax,
                              std::to_string(err.source().begin.line) + ": " +
                                std::string(err.description()) } };
  }

  return getConfig(tbl);
}

OptError<>
saveDefaultConfig(const std::filesystem::path& path) {
  const auto dir = path.parent_path();

  std::error_code ec;

  if (!std::filesystem::is_directory(dir)) {
    std::filesystem::create_directories(dir, ec);
  }

  if (ec) {
    return Error::make(ec.message());
  }

  std::ofstream ofs(path);
  ofs << default_config;

  if (ofs.fail()) {
    return Error::make("Error writing config");
  }

  return {};
}

YaftConfigAndError
loadConfigOrMakeDefault(const std::filesystem::path& path) {
  auto result = loadConfig(path);
  if (!result.err.has_value()) {
    return result;
  }

  auto& err = *result.err;
  if (err.type == YaftConfigError::Missing) {
    err.msg = "No config, creating new one\r\n";
    if (const auto optErr = saveDefaultConfig(path); !optErr.has_value()) {
      err.msg += optErr.error().msg + "\r\n";
    }
  } else {
    err.msg = "Config syntax issues:\r\n" + err.msg;
  }

  return result;
}
