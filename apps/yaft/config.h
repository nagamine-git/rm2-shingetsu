#pragma once

#include "Error.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "MathUtil.h"
#include "keymap.h"
#include "layout.h"

// Japanese input (see apps/yaft/ime).
struct ImeSettings {
  // Show the IME bar and let the input method see keys.
  bool enabled = true;

  // "colemak", "qwerty" or the path of a hazkey-style TSV.
  std::string table = "colemak";

  // Escape in direct mode switches the input method off.
  bool escapeOff = true;

  // Where the mozc helper's chroot lives, and who it runs as.
  std::string mozcRoot = "/home/root/.local/share/rm2-shingetsu/mozc-root";
  int mozcUid = 1000;

  // If not empty, run this command instead of the chroot helper.
  std::vector<std::string> mozcCommand;

  bool operator==(const ImeSettings& o) const {
    return enabled == o.enabled && table == o.table &&
           escapeOff == o.escapeOff && mozcRoot == o.mozcRoot &&
           mozcUid == o.mozcUid && mozcCommand == o.mozcCommand;
  }
  bool operator!=(const ImeSettings& o) const { return !(*this == o); }
};

struct YaftConfig {

  // Layout for virtual keyboard.
  const Layout* layout = &colemak_layout;

  // Keymap of physical keyboard.
  const KeyMap* keymap = &rm_colemak_keymap;

  bool autoRotate = true;
  rmlib::Rotation rotation = rmlib::Rotation::None;

  // Auto refresh full screen after 1024 updates.
  // Set to 0 to disable.
  int autoRefresh = 0;

  // Key repeat delay in ms
  int repeatDelay = 600;
  // Key repeat rate in chars / sec.
  int repeatRate = 25;

  ImeSettings ime;

  static YaftConfig getDefault();
};

struct YaftConfigError {
  enum { Missing, Syntax } type;
  std::string msg;
};

struct YaftConfigAndError {
  YaftConfig config;

  std::optional<YaftConfigError> err;
};

std::filesystem::path
getConfigPath();

/// Load the config from the `~/.config/yaft/config.toml` location.
YaftConfigAndError
loadConfig(const std::filesystem::path& path);

OptError<>
saveDefaultConfig(const std::filesystem::path& path);

/// Always returns a config, either the default one or the one on the file
/// system. Will also make a new config file if it didn't exist.
///
/// If any error occured during the loading of the config, it's also returned.
YaftConfigAndError
loadConfigOrMakeDefault(const std::filesystem::path& path);
