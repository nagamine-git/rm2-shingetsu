Yaft
====

Fast framebuffer terminal emulator, using [libghostty](https://ghostty.org/docs/about#libghostty) for VT parsing.

Usage
-----

To use simply execute `yaft` or `yaft <command..>`.

There's an on-screen keyboard that can be partially hidden by long pressing the escape key.
When the type folio is attached the screen will go into landscape mode and the keyboard will be hidden.
Mouse events are supported using the touchscreen.

Japanese input
--------------

Yaft has a built-in input method for Japanese: kana are composed locally with
a hazkey (azooKey) style romantable (by default the Shingetsu layout from
[shingetsu-layout](https://github.com/nagamine-git/shingetsu-layout)), and
[mozc](https://github.com/google/mozc) converts them to kanji.

* `Ctrl+Space` toggles it. With the `rm-colemak` keymap a tap of the right Alt
  key switches it on and a tap of the left Alt key off (holding them is Alt,
  and so is chording them with another key). The `colemak` (USB) keymap does
  the same with the Cmd/Win keys and also knows the Japanese keyboard's
  Muhenkan/Henkan/Zenkaku keys.
* While typing, the text being composed is shown on the input bar between the
  terminal and the keyboard. `Space` starts the conversion, `Space`/`Down`/`Tab`
  pick the next candidate, `Up` the previous one, `Left`/`Right` change the
  segment (with `Shift` to resize it), digits move to a candidate on the
  list, `Enter` commits and `Escape`/`Backspace` go back to the kana.
* `Enter` right after typing commits the kana as they are, `Escape` cancels, and
  `Escape` while nothing is being composed switches the input method off
  (`escape-off`), so vim's insert to normal mode switch is also one to
  English.
* mozc runs as a helper process in a chroot, see `mozc-root` in the config. It
  is started in the background, the bar reports "mozc 起動中" until it's ready.

`yaft-ime-cli` runs the same logic without a screen, for testing:
```sh
yaft-ime-cli --keys 'sidfyvshsl{space}{enter}' --mozc-command mozc_emacs_helper --suppress_stderr
COMMIT	私の名前
```

Wide (Japanese) glyphs come from `shingetsu/fonts`.

Config
------

A config file is created in `.config/yaft/config.toml` on startup.
For syntax see [config.cpp](./config.cpp). The default config is:
```toml
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
```

Credits
-------

Yaft was originally forked from https://uobikiemukot.github.io/yaft/.
Original author: Copyright (c) 2012 haru <uobikiemukot at gmail dot com>
