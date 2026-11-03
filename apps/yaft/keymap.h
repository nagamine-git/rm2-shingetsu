#pragma once

#include <initializer_list>
#include <string_view>
#include <unordered_map>

struct EvKeyInfo {
  int scancode;

  int shfitScancode = 0; // Code when pressed with shift.

  int mod1Scancode = 0; // Left-alt scancode
  int mod2Scancode = 0; // Right-alt scancode

  // Tap-hold: `scancode` is sent on a tap (a press and release with no other
  // key pressed in between), `holdCode` acts as a modifier while held.
  int holdCode = 0;
};

using KeyMap = std::unordered_map<int, EvKeyInfo>;

extern const KeyMap qwerty_keymap;
extern const KeyMap rm_qwerty_keymap;
extern const KeyMap timower_keymap;
extern const KeyMap rm_colemak_keymap;
extern const KeyMap colemak_keymap;

// Name mapping for config file
const std::initializer_list<std::pair<std::string_view, const KeyMap*>>
  keymaps = {
    { "qwerty", &qwerty_keymap },   { "rm-qwerty", &rm_qwerty_keymap },
    { "timower", &timower_keymap }, { "rm-colemak", &rm_colemak_keymap },
    { "colemak", &colemak_keymap },
  };
