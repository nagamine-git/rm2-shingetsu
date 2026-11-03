#include <catch2/catch_test_macros.hpp>

#include "FakeConverter.h"
#include "TempFiles.h"
#include "rMLibTestHelper.h"

#include "YaftWidget.h"
#include "terminal_adapter.h"

#include <fstream>

#include <fcntl.h>
#include <unistd.h>

using namespace rmlib;

namespace {

const Layout test_layout = { {
  { { "a", makeCallback(1) },
    { "b", makeCallback(2), "", 0, /* width */ 2 },
    { "c", makeCallback(3) } },

  {
    { "ctrl", Ctrl },
    { "alt", Alt },
    { ":shift", Shift },
    { "<>", makeCallback(4), "^", makeCallback(5), 1, makeCallback(6) },
  },
} };

template<typename RO>
void
doKey(TestContext& ctx,
      const FindResults<RO>& ros,
      std::string_view name,
      bool down,
      const Layout& layout = test_layout) {

  bool found = false;
  int rowIdx = 0;
  int colIdx = 0;
  for (const auto& row : layout.rows) {
    for (const auto& key : row) {
      if (key.name == name) {
        found = true;
        break;
      }

      colIdx += key.width;
    }

    if (found) {
      break;
    }

    rowIdx++;
    colIdx = 0;
  }

  INFO("Key " << name << " not found");
  REQUIRE(found);

  float x = (float(colIdx) + 0.5f) / layout.numCols();
  float y = (float(rowIdx) + 0.5f) / layout.numRows();

  ctx.sendInput(down, ros, { x, y });
}

template<typename RO>
void
tapKey(TestContext& ctx,
       const FindResults<RO>& ros,
       std::string_view name,
       const Layout& layout = test_layout) {
  doKey(ctx, ros, name, true, layout);
  ctx.pump();
  doKey(ctx, ros, name, false, layout);
}

} // namespace

TEST_CASE("Keyboard", "[yaft][ui]") {
  auto ctx = TestContext::make();

  int lastCallback = -1;
  int callbackCount = 0;
  const auto updateCb = [&](auto cb) {
    if (lastCallback == cb) {
      callbackCount++;
    }
    lastCallback = cb;
  };

  const auto params = KeyboardParams{
    .layout = test_layout,
    .keymap = qwerty_keymap,
    .repeatDelay = std::chrono::milliseconds(100),
    .repeatTime = std::chrono::milliseconds(50),
  };

  constexpr auto width = 300;
  constexpr auto height = 200;
  ctx.pumpWidget(
    Center(Sized(Keyboard(nullptr, params, updateCb), width, height)));

  auto kbd = ctx.findByType<Keyboard>();

  REQUIRE_THAT(kbd, ctx.matchesGolden("yaft-keyboard.png"));

  // Test key feedback when pressing.
  doKey(ctx, kbd, "a", true);
  ctx.pump(params.repeatDelay + 3 * params.repeatTime);
  REQUIRE_THAT(kbd, ctx.matchesGolden("yaft-keyboard-down.png"));
  doKey(ctx, kbd, "a", false);
  REQUIRE(lastCallback == 1);

  // Make sure repeat works.
  CHECK(callbackCount >= 2);

  // Also make sure a wide key gets updated correctly.
  doKey(ctx, kbd, "b", true);
  ctx.pump(std::chrono::milliseconds(5));
  REQUIRE_THAT(kbd, ctx.matchesGolden("yaft-keyboard-down2.png"));
  doKey(ctx, kbd, "b", false);
  REQUIRE(lastCallback == 2);

  tapKey(ctx, kbd, "c");
  REQUIRE(lastCallback == 3);

  tapKey(ctx, kbd, ":shift");
  REQUIRE_THAT(kbd, ctx.matchesGolden("yaft-keyboard-held.png"));
  tapKey(ctx, kbd, "<>");
  REQUIRE(lastCallback == 5);

  tapKey(ctx, kbd, "<>");
  REQUIRE(lastCallback == 4);

  doKey(ctx, kbd, ":shift", true);
  ctx.pump(params.repeatDelay + params.repeatTime);
  REQUIRE_THAT(kbd, ctx.matchesGolden("yaft-keyboard-stuck.png"));
  doKey(ctx, kbd, ":shift", false);

  tapKey(ctx, kbd, "<>");
  REQUIRE(lastCallback == 5);

  tapKey(ctx, kbd, "<>");
  REQUIRE(lastCallback == 5);
  tapKey(ctx, kbd, ":shift");

  tapKey(ctx, kbd, "<>");
  REQUIRE(lastCallback == 4);

  doKey(ctx, kbd, "<>", true);
  ctx.pump(params.repeatDelay + params.repeatTime);
  doKey(ctx, kbd, "<>", false);
  REQUIRE(lastCallback == 6);
}

TEST_CASE("Yaft", "[yaft][ui]") {
  auto ctx = TestContext::make();

  std::string program = CAT_EXE;
  std::vector<char*> args = { program.data(), nullptr };

  YaftConfigAndError cfgAndErr;
  cfgAndErr.config = YaftConfig::getDefault();

  // The goldens below predate the Colemak and input method defaults.
  cfgAndErr.config.layout = &qwerty_layout;
  cfgAndErr.config.keymap = &rm_qwerty_keymap;
  cfgAndErr.config.ime.enabled = false;

  SECTION("landscape") {
    cfgAndErr.config.rotation = rmlib::Rotation::Clockwise;
    cfgAndErr.config.layout = &empty_layout;
    cfgAndErr.err = YaftConfigError{ YaftConfigError::Missing, "Error test!" };

    ctx.pumpWidget(Yaft(args.front(), args.data(), cfgAndErr));
    ctx.pump();

    auto yaft = ctx.findByType<Yaft>();
    REQUIRE_THAT(yaft, ctx.matchesGolden("yaft-landscape.png"));
  }

  SECTION("portrait") {
    cfgAndErr.config.rotation = rmlib::Rotation::None;

    ctx.pumpWidget(Yaft(args.front(), args.data(), cfgAndErr));
    ctx.pump();

    auto yaft = ctx.findByType<Yaft>();
    auto kbd = ctx.findByType<Keyboard>();
    REQUIRE_THAT(yaft, ctx.matchesGolden("yaft-init.png"));

    const auto down = [&](auto name) {
      doKey(ctx, kbd, name, true, qwerty_layout);
    };
    const auto up = [&](auto name) {
      doKey(ctx, kbd, name, false, qwerty_layout);
    };
    const auto tap = [&](auto name) { tapKey(ctx, kbd, name, qwerty_layout); };

    tap("a");
    tap(":shift");
    tap("a");
    tap(":enter");
    ctx.pump();
    REQUIRE_THAT(yaft, ctx.matchesGolden("yaft-aA.png"));

    SECTION("Hide") {
      down("esc");
      ctx.pump(std::chrono::milliseconds(1200));
      up("esc");
      REQUIRE_THAT(yaft, ctx.matchesGolden("yaft-hidden.png"));
    }

    // Regression test: moving the cursor onto blank rows with no other
    // content change must still erase its highlight from the row it left.
    // Ghostty's own dirty tracking never covers cursor position (only cell
    // content/palette/selection etc.), so this only passes if
    // ScreenRenderObject independently tracks the cursor's previous row.
    SECTION("Cursor redraw") {
      tap(":enter");
      tap(":enter");
      ctx.pump();
      REQUIRE_THAT(yaft, ctx.matchesGolden("yaft-cursor-moved.png"));
    }

    SECTION("Exit") {
      tap(":enter");
      tap("ctrl");
      tap("d");
      REQUIRE(ctx.shouldStop());
    }
  }
}

// Regression test for the Terminal adapter's per-row dirty tracking (backed
// by libghostty-vt's native render-state dirty state): only rows that
// actually changed should come back dirty, not the whole screen.
TEST_CASE("Terminal dirty tracking", "[yaft]") {
  Terminal term(400, 800);

  auto feed = [&](const char* s) {
    term.parse(reinterpret_cast<const uint8_t*>(s), strlen(s));
  };
  auto dirtyRows = [&] {
    term.beginDraw();
    std::vector<int> rows;
    for (int i = 0; term.nextRow(); i++) {
      if (term.isRowDirty()) {
        rows.push_back(i);
      }
    }
    return rows;
  };
  auto clearAll = [&] {
    term.beginDraw();
    while (term.nextRow()) {
      term.clearRowDirty();
    }
  };

  INFO("cols=" << term.cols << " lines=" << term.lines);

  feed("hello\r\n");
  auto dirty1 = dirtyRows();
  CHECK_FALSE(dirty1.empty());
  clearAll();

  // Write far from row 0; row 0 must not show up dirty again, and the
  // number of dirty rows must be much smaller than the total row count.
  feed("\x1b[10;1Hworld");
  auto dirty2 = dirtyRows();
  CHECK_FALSE(dirty2.empty());
  CHECK(dirty2.size() < size_t(term.lines) / 2);
  CHECK(std::find(dirty2.begin(), dirty2.end(), 0) == dirty2.end());
  CHECK(std::find(dirty2.begin(), dirty2.end(), 9) != dirty2.end());
  clearAll();

  // No new writes: nothing should be dirty.
  auto dirty3 = dirtyRows();
  CHECK(dirty3.empty());
}

TEST_CASE("inotify", "[yaft][ui]") {
  TemporaryDirectory tmp;
  setenv("HOME", tmp.dir.c_str(), 1);
  setenv("XDG_CONFIG_HOME", "", 1);

  std::string program = CAT_EXE;
  std::vector<char*> args = { program.data(), nullptr };

  auto ctx = TestContext::make();
  ctx.pumpWidget(Yaft(args.front(), args.data(), getConfigPath()));

  auto yaft = ctx.findByType<Yaft>();
  REQUIRE_THAT(yaft, ctx.matchesGolden("yaft-new.png"));

  std::ofstream cfgOfs(tmp.dir / ".config" / "yaft" / "config.toml");
  cfgOfs << "rotation = \"inverted\"";
  cfgOfs.close();

  ctx.pump(std::chrono::milliseconds(100));
  REQUIRE_THAT(yaft, ctx.matchesGolden("yaft-inverted.png"));
}

namespace {

// A terminal whose pty is a pipe, so tests can read back what was typed.
struct CapturedTerminal {
  CapturedTerminal() : term(400, 800) {
    REQUIRE(pipe2(fds, O_NONBLOCK | O_CLOEXEC) == 0);
    term.fd = fds[1];
  }
  ~CapturedTerminal() {
    close(fds[0]);
    close(fds[1]);
    term.fd = -1;
  }
  CapturedTerminal(const CapturedTerminal&) = delete;
  CapturedTerminal& operator=(const CapturedTerminal&) = delete;

  std::string output() const {
    std::string out;
    char buf[256];
    ssize_t n = 0;
    while ((n = read(fds[0], buf, sizeof(buf))) > 0) {
      out.append(buf, size_t(n));
    }
    return out;
  }

  int fds[2];
  Terminal term;
};

// A keyboard widget with an input method, fed with physical key events.
struct PhysicalKeyboard {
  explicit PhysicalKeyboard(const KeyMap& keymap,
                            const Layout& layout = empty_layout)
    : ime(*ime::Composer::load("colemak"), &converter) {
    const auto params = KeyboardParams{
      .layout = layout,
      .keymap = keymap,
      .repeatDelay = std::chrono::milliseconds(600),
      .repeatTime = std::chrono::milliseconds(40),
      .ime = &ime,
      .imeActivity = [this] { activity++; },
    };
    ctx.pumpWidget(
      Center(Sized(Keyboard(&terminal.term, params, [](int) {}), 300, 200)));
  }

  void key(int scancode, bool down) {
    SDL_Event ev{};
    ev.type = down ? SDL_KEYDOWN : SDL_KEYUP;
    ev.key.keysym.scancode = SDL_Scancode(scancode);
    ctx.sendEv(ev);
  }
  void down(int scancode) { key(scancode, true); }
  void up(int scancode) { key(scancode, false); }
  void tap(int scancode) {
    down(scancode);
    up(scancode);
  }

  TestContext ctx = TestContext::make();
  CapturedTerminal terminal;
  FakeConverter converter;
  ime::ImeController ime;
  int activity = 0;
};

} // namespace

TEST_CASE("Colemak keymaps", "[yaft]") {
  const std::vector<int> top = { KEY_Q, KEY_W, KEY_E, KEY_R, KEY_T,
                                 KEY_Y, KEY_U, KEY_I, KEY_O, KEY_P };
  const std::vector<int> home = { KEY_A, KEY_S, KEY_D, KEY_F, KEY_G,
                                  KEY_H, KEY_J, KEY_K, KEY_L, KEY_SEMICOLON };
  const std::vector<int> bottom = { KEY_Z, KEY_X, KEY_C, KEY_V,
                                    KEY_B, KEY_N, KEY_M };

  const auto checkLetters = [](const KeyMap& map,
                               const std::vector<int>& keys,
                               std::string_view letters) {
    REQUIRE(keys.size() == letters.size());
    for (size_t i = 0; i < keys.size(); i++) {
      INFO("key " << keys[i] << " should give " << letters[i]);
      REQUIRE(map.count(keys[i]) == 1);
      CHECK(map.at(keys[i]).scancode == toupper(letters[i]));
    }
  };

  for (const auto* map : { &rm_colemak_keymap, &colemak_keymap }) {
    checkLetters(*map, top, "qwfpgjluy;");
    checkLetters(*map, home, "arstdhneio");
    checkLetters(*map, bottom, "zxcvbkm");

    // ';' lives where P is, with ':' on shift.
    CHECK(map->at(KEY_P).shfitScancode == ':');
  }

  SECTION("rm-colemak keeps the rm-qwerty keys") {
    for (const auto& [code, info] : rm_qwerty_keymap) {
      if (code == KEY_CAPSLOCK || code == KEY_LEFTALT || code == KEY_RIGHTALT ||
          std::find(top.begin(), top.end(), code) != top.end() ||
          std::find(home.begin(), home.end(), code) != home.end() ||
          code == KEY_N) {
        continue;
      }
      INFO("key " << code);
      REQUIRE(rm_colemak_keymap.count(code) == 1);
      CHECK(rm_colemak_keymap.at(code).scancode == info.scancode);
      CHECK(rm_colemak_keymap.at(code).shfitScancode == info.shfitScancode);
    }
    CHECK(rm_colemak_keymap.at(KEY_END).scancode == Mod1);
    CHECK(rm_colemak_keymap.at(KEY_1).shfitScancode == '!');

    const auto& caps = rm_colemak_keymap.at(KEY_CAPSLOCK);
    CHECK(caps.scancode == Escape);
    CHECK(caps.holdCode == Ctrl);
    const auto& left = rm_colemak_keymap.at(KEY_LEFTALT);
    CHECK(left.scancode == ImeOff);
    CHECK(left.holdCode == Alt);
    const auto& right = rm_colemak_keymap.at(KEY_RIGHTALT);
    CHECK(right.scancode == ImeOn);
    CHECK(right.holdCode == Alt);
  }

  SECTION("usb colemak") {
    CHECK(colemak_keymap.at(KEY_CAPSLOCK).scancode == Escape);
    CHECK(colemak_keymap.at(KEY_CAPSLOCK).holdCode == Ctrl);
    CHECK(colemak_keymap.at(KEY_LEFTMETA).scancode == ImeOff);
    CHECK(colemak_keymap.at(KEY_LEFTMETA).holdCode == Alt);
    CHECK(colemak_keymap.at(KEY_RIGHTMETA).scancode == ImeOn);
    CHECK(colemak_keymap.at(KEY_RIGHTMETA).holdCode == Alt);
    CHECK(colemak_keymap.at(KEY_MUHENKAN).scancode == ImeOff);
    CHECK(colemak_keymap.at(KEY_HANJA).scancode == ImeOff);
    CHECK(colemak_keymap.at(KEY_HENKAN).scancode == ImeOn);
    CHECK(colemak_keymap.at(KEY_HANGEUL).scancode == ImeOn);
    CHECK(colemak_keymap.at(KEY_ZENKAKUHANKAKU).scancode == ImeToggle);
    CHECK(colemak_keymap.at(KEY_LEFTALT).scancode == Alt);
    CHECK(colemak_keymap.at(KEY_SPACE).scancode == ' ');
  }

  SECTION("registered by name") {
    const auto find = [](std::string_view name) -> const KeyMap* {
      for (const auto& [mapName, map] : keymaps) {
        if (mapName == name) {
          return map;
        }
      }
      return nullptr;
    };
    CHECK(find("rm-colemak") == &rm_colemak_keymap);
    CHECK(find("colemak") == &colemak_keymap);
    CHECK(find("qwerty") == &qwerty_keymap);
    CHECK(find("rm-qwerty") == &rm_qwerty_keymap);
  }
}

TEST_CASE("Colemak layout", "[yaft]") {
  const auto names =
    [](const std::vector<KeyInfo>& row, size_t from, size_t n) {
      std::string out;
      for (size_t i = from; i < from + n; i++) {
        out += row[i].name;
      }
      return out;
    };

  REQUIRE(colemak_layout.numRows() == qwerty_layout.numRows());
  CHECK(colemak_layout.numCols() == qwerty_layout.numCols());

  CHECK(names(colemak_layout.rows[2], 0, 10) == "qwfpgjluy;");
  CHECK(names(colemak_layout.rows[3], 0, 10) == "arstdhneio");
  CHECK(names(colemak_layout.rows[4], 1, 8) == "zxcvbkm:");

  // Only the letters moved: the codes follow the names.
  for (const auto& row : colemak_layout.rows) {
    for (const auto& key : row) {
      if (key.name.size() == 1 && isalpha(key.name[0]) != 0) {
        CHECK(key.code == toupper(key.name[0]));
      }
    }
  }
  CHECK(colemak_layout.rows[2][9].code == ';');
  CHECK(colemak_layout.rows[4][8].code == ':');
  CHECK(colemak_layout.rows[2].back().code == Enter);

  bool found = false;
  for (const auto& [name, layout] : layouts) {
    if (name == "colemak") {
      found = layout == &colemak_layout;
    }
  }
  CHECK(found);
}

TEST_CASE("Physical keys: Colemak output", "[yaft][ui]") {
  PhysicalKeyboard kbd(rm_colemak_keymap);

  SECTION("letters") {
    kbd.tap(KEY_S);
    kbd.tap(KEY_SEMICOLON);
    kbd.tap(KEY_P);
    CHECK(kbd.terminal.output() == "ro;");
  }

  SECTION("shift") {
    kbd.down(KEY_LEFTSHIFT);
    kbd.tap(KEY_S);
    kbd.tap(KEY_P);
    kbd.up(KEY_LEFTSHIFT);
    CHECK(kbd.terminal.output() == "R:");
  }

  SECTION("caps lock taps escape") {
    kbd.tap(KEY_CAPSLOCK);
    CHECK(kbd.terminal.output() == "\x1b");
  }

  SECTION("caps lock held is control") {
    kbd.down(KEY_CAPSLOCK);
    kbd.tap(KEY_C);
    kbd.up(KEY_CAPSLOCK);
    CHECK(kbd.terminal.output() == "\x03");
  }
}

TEST_CASE("Physical keys: input method keys", "[yaft][ui]") {
  PhysicalKeyboard kbd(rm_colemak_keymap);
  auto& ime = kbd.ime;
  REQUIRE_FALSE(ime.enabled());

  SECTION("tapping the alt keys switches the input method") {
    kbd.tap(KEY_RIGHTALT);
    CHECK(ime.enabled());
    CHECK(kbd.activity > 0);

    kbd.tap(KEY_LEFTALT);
    CHECK_FALSE(ime.enabled());

    // Nothing leaked to the terminal.
    CHECK(kbd.terminal.output().empty());
  }

  SECTION("an alt chord is just alt") {
    kbd.tap(KEY_RIGHTALT);
    REQUIRE(ime.enabled());

    kbd.down(KEY_LEFTALT);
    kbd.tap(KEY_X);
    kbd.up(KEY_LEFTALT);
    CHECK(ime.enabled());
    CHECK(kbd.terminal.output() == "\x1bx");

    // Also with the other alt key, and with the order of release swapped.
    kbd.down(KEY_RIGHTALT);
    kbd.down(KEY_X);
    kbd.up(KEY_RIGHTALT);
    kbd.up(KEY_X);
    CHECK(ime.enabled());
    CHECK(kbd.terminal.output() == "\x1bx");
  }

  SECTION("chording never turns the input method on either") {
    kbd.down(KEY_RIGHTALT);
    kbd.tap(KEY_X);
    kbd.up(KEY_RIGHTALT);
    CHECK_FALSE(ime.enabled());
  }

  SECTION("alt held for a while is not a tap") {
    kbd.tap(KEY_RIGHTALT);
    kbd.down(KEY_LEFTALT);
    kbd.ctx.pump(std::chrono::milliseconds(300));
    kbd.up(KEY_LEFTALT);
    CHECK(ime.enabled());
  }

  SECTION("typing Japanese") {
    kbd.tap(KEY_RIGHTALT);
    kbd.tap(KEY_S); // r -> か
    kbd.tap(KEY_G); // d -> た
    CHECK(kbd.terminal.output().empty());
    CHECK(ime.view().preedit.at(0).text == "かた");

    // Turning it off commits what is being composed.
    kbd.tap(KEY_LEFTALT);
    CHECK(kbd.terminal.output() == "かた");
    CHECK_FALSE(ime.enabled());
  }

  SECTION("caps lock is escape in the input method too") {
    kbd.tap(KEY_RIGHTALT);
    kbd.tap(KEY_S);
    kbd.tap(KEY_CAPSLOCK); // cancels the composition
    CHECK(ime.enabled());
    CHECK(kbd.terminal.output().empty());

    kbd.tap(KEY_CAPSLOCK); // switches the input method off
    CHECK_FALSE(ime.enabled());
    CHECK(kbd.terminal.output() == "\x1b");
  }

  SECTION("enter commits, other keys pass through") {
    kbd.tap(KEY_RIGHTALT);
    kbd.tap(KEY_S);
    kbd.tap(KEY_ENTER);
    CHECK(kbd.terminal.output() == "か");

    kbd.tap(KEY_SPACE);
    kbd.tap(KEY_BACKSPACE);
    CHECK(kbd.terminal.output() == " \x7f");
  }

  SECTION("space converts") {
    kbd.tap(KEY_RIGHTALT);
    kbd.tap(KEY_S);
    kbd.tap(KEY_SPACE);
    CHECK(ime.view().mode == ime::ImeView::Mode::Converting);
    REQUIRE(kbd.converter.calls.size() == 1);
    CHECK(kbd.converter.calls[0].front() == "97:か");
    CHECK(kbd.terminal.output().empty());
  }
}

TEST_CASE("Physical keys: usb input method keys", "[yaft][ui]") {
  PhysicalKeyboard kbd(colemak_keymap);
  auto& ime = kbd.ime;

  kbd.tap(KEY_HENKAN);
  CHECK(ime.enabled());
  kbd.tap(KEY_MUHENKAN);
  CHECK_FALSE(ime.enabled());
  kbd.tap(KEY_HANGEUL);
  CHECK(ime.enabled());
  kbd.tap(KEY_HANJA);
  CHECK_FALSE(ime.enabled());

  kbd.tap(KEY_ZENKAKUHANKAKU);
  CHECK(ime.enabled());
  kbd.tap(KEY_ZENKAKUHANKAKU);
  CHECK_FALSE(ime.enabled());

  kbd.tap(KEY_RIGHTMETA);
  CHECK(ime.enabled());
  kbd.tap(KEY_LEFTMETA);
  CHECK_FALSE(ime.enabled());

  // A held key must not toggle again on key repeat.
  kbd.down(KEY_ZENKAKUHANKAKU);
  kbd.ctx.pump(std::chrono::milliseconds(800));
  kbd.up(KEY_ZENKAKUHANKAKU);
  CHECK(ime.enabled());

  SECTION("meta chords are alt") {
    kbd.down(KEY_LEFTMETA);
    kbd.tap(KEY_X);
    kbd.up(KEY_LEFTMETA);
    CHECK(ime.enabled());
    CHECK(kbd.terminal.output() == "\x1bx");
  }
}

TEST_CASE("On-screen keyboard: input method", "[yaft][ui]") {
  auto ctx = TestContext::make();
  CapturedTerminal terminal;
  FakeConverter converter;
  ime::ImeController ime(*ime::Composer::load("colemak"), &converter);

  const auto params = KeyboardParams{
    .layout = colemak_layout,
    .keymap = rm_colemak_keymap,
    .repeatDelay = std::chrono::milliseconds(600),
    .repeatTime = std::chrono::milliseconds(40),
    .ime = &ime,
  };
  ctx.pumpWidget(
    Center(Sized(Keyboard(&terminal.term, params, [](int) {}), 1000, 500)));
  auto kbd = ctx.findByType<Keyboard>();

  const auto tap = [&](auto name) { tapKey(ctx, kbd, name, colemak_layout); };

  // Without the input method the letters are plain Colemak letters.
  tap("r");
  tap("s");
  tap("t");
  CHECK(terminal.output() == "rst");

  // Ctrl+Space switches it on, then keys are composed.
  tap("ctrl");
  tap(" ");
  REQUIRE(ime.enabled());

  tap("a");
  tap("i");
  CHECK(ime.view().preedit.at(0).text == "ば");

  tap(":enter");
  CHECK(terminal.output() == "ば");

  tap("ctrl");
  tap(" ");
  CHECK_FALSE(ime.enabled());
}

TEST_CASE("CJK glyphs", "[yaft]") {
  Terminal term(400, 800);
  const auto feed = [&](std::string_view s) {
    term.parse(reinterpret_cast<const uint8_t*>(s.data()), s.size());
  };
  feed("日本語ｱ A");

  term.beginDraw();
  REQUIRE(term.nextRow());

  const auto inked = [](const glyph_t& glyph) {
    int count = 0;
    for (const auto row : glyph.bitmap) {
      count += __builtin_popcount(row);
    }
    return count;
  };

  const auto nichi = term.cellAt(0);
  REQUIRE(nichi.glyph != nullptr);
  CHECK(nichi.width == CellWidth::Wide);
  CHECK(nichi.glyph->code == 0x65E5);
  CHECK(nichi.glyph->width == 2);
  CHECK(inked(*nichi.glyph) > 100);

  // The right half of a wide cell reuses the glyph.
  const auto tail = term.cellAt(1);
  CHECK(tail.width == CellWidth::NextToWide);
  CHECK(tail.glyph == nichi.glyph);

  CHECK(term.cellAt(2).glyph->code == 0x672C);
  CHECK(term.cellAt(4).glyph->code == 0x8A9E);

  // Half width katakana take a single cell.
  const auto ka = term.cellAt(6);
  CHECK(ka.width == CellWidth::Half);
  CHECK(ka.glyph->code == 0xFF71);
  CHECK(ka.glyph->width == 1);
  CHECK(inked(*ka.glyph) > 10);

  CHECK(term.cellAt(8).glyph->code == 'A');

  SECTION("bold reuses the glyph") {
    feed("\r\n\x1b[1m日");
    term.beginDraw();
    REQUIRE(term.nextRow());
    REQUIRE(term.nextRow());
    const auto bold = term.cellAt(0);
    CHECK(bold.bold);
    CHECK(bold.glyph->code == 0x65E5);
    CHECK(bold.boldGlyph == bold.glyph);
  }

  SECTION("lookup") {
    for (const uint32_t code : { 0x3042U,
                                 0x30A2U,
                                 0x6F22U,
                                 0x3001U,
                                 0x300CU,
                                 0xFF01U,
                                 0xFFE5U,
                                 0x9FA0U }) {
      INFO("U+" << std::hex << code);
      const auto* glyph = lookupGlyph(code);
      REQUIRE(glyph != nullptr);
      CHECK(glyph->code == code);
      CHECK(glyph->width == 2);
    }

    // The builtin font keeps priority (the blank ideographic space).
    const auto* space = lookupGlyph(0x3000);
    REQUIRE(space != nullptr);
    CHECK(space->width == 2);
    CHECK(inked(*space) == 0);

    CHECK(lookupGlyph('A')->width == 1);
    CHECK(lookupGlyph(0x10FFFF) == nullptr);
    // Ambiguous/neutral characters stay one cell wide: no wide glyph.
    CHECK(lookupGlyph(0x303F) == nullptr);
  }
}

TEST_CASE("Config: input method", "[yaft]") {
  TemporaryDirectory tmp;
  const auto path = tmp.dir / "config.toml";
  const auto load = [&](std::string_view content) {
    std::ofstream(path) << content;
    return loadConfig(path);
  };

  SECTION("defaults") {
    const auto cfg = YaftConfig::getDefault();
    CHECK(cfg.layout == &colemak_layout);
    CHECK(cfg.keymap == &rm_colemak_keymap);
    CHECK(cfg.ime.enabled);
    CHECK(cfg.ime.table == "colemak");
    CHECK(cfg.ime.escapeOff);
    CHECK(cfg.ime.mozcRoot ==
          "/home/root/.local/share/rm2-shingetsu/mozc-root");
    CHECK(cfg.ime.mozcUid == 1000);
    CHECK(cfg.ime.mozcCommand.empty());
  }

  SECTION("the default config file parses without complaints") {
    REQUIRE(saveDefaultConfig(path).has_value());
    const auto result = loadConfig(path);
    CHECK_FALSE(result.err.has_value());
    CHECK(result.config.layout == &colemak_layout);
    CHECK(result.config.keymap == &rm_colemak_keymap);
    CHECK(result.config.ime == YaftConfig::getDefault().ime);
    CHECK(result.config.autoRefresh == 1024);
  }

  SECTION("configs without [ime] keep working") {
    const auto result =
      load("layout = \"qwerty\"\nkeymap = \"rm-qwerty\"\nrepeat-rate = 10\n");
    CHECK_FALSE(result.err.has_value());
    CHECK(result.config.layout == &qwerty_layout);
    CHECK(result.config.keymap == &rm_qwerty_keymap);
    CHECK(result.config.repeatRate == 10);
    CHECK(result.config.ime.enabled);
  }

  SECTION("all keys") {
    const auto result =
      load("keymap = \"colemak\"\n"
           "[ime]\n"
           "enabled = false\n"
           "table = \"/tmp/my.tsv\"\n"
           "escape-off = false\n"
           "mozc-root = \"/opt/mozc\"\n"
           "mozc-uid = 1234\n"
           "mozc-command = [\"ssh\", \"host\", \"mozc_emacs_helper\"]\n");
    CHECK_FALSE(result.err.has_value());
    CHECK(result.config.keymap == &colemak_keymap);
    CHECK_FALSE(result.config.ime.enabled);
    CHECK(result.config.ime.table == "/tmp/my.tsv");
    CHECK_FALSE(result.config.ime.escapeOff);
    CHECK(result.config.ime.mozcRoot == "/opt/mozc");
    CHECK(result.config.ime.mozcUid == 1234);
    CHECK(result.config.ime.mozcCommand ==
          std::vector<std::string>{ "ssh", "host", "mozc_emacs_helper" });
  }

  SECTION("mistakes are reported") {
    auto result = load("[ime]\nbogus = 1\n");
    REQUIRE(result.err.has_value());
    CHECK(result.err->msg.find("ime.bogus") != std::string::npos);
    CHECK(result.config.ime.enabled);

    result = load("ime = 3\n");
    REQUIRE(result.err.has_value());
    CHECK(result.err->msg.find("ime") != std::string::npos);

    result = load("[ime]\nmozc-command = [1, 2]\n");
    REQUIRE(result.err.has_value());
    CHECK(result.config.ime.mozcCommand.empty());
  }
}

// The whole thing: the Yaft widget with its input method, talking to a
// helper that replays a recorded mozc session.
TEST_CASE("Yaft: input method bar", "[yaft][ui]") {
  TemporaryDirectory tmp;
  const auto script = tmp.dir / "helper.sh";
  std::ofstream(script) << mozc_replay_script;

  auto ctx = TestContext::make();
  std::string program = CAT_EXE;
  std::vector<char*> args = { program.data(), nullptr };

  YaftConfigAndError cfgAndErr;
  cfgAndErr.config = YaftConfig::getDefault();
  cfgAndErr.config.ime.mozcCommand = {
    "/bin/sh",
    script.string(),
    (assets_path / "mozc" / "session1.out").string(),
  };

  ctx.pumpWidget(Yaft(args.front(), args.data(), cfgAndErr));
  // Lets the helper finish its handshake.
  ctx.pump(std::chrono::milliseconds(500));

  auto yaft = ctx.findByType<Yaft>();
  auto kbd = ctx.findByType<Keyboard>();
  const auto tap = [&](auto name) { tapKey(ctx, kbd, name, colemak_layout); };

  SECTION("off") {
    REQUIRE_THAT(yaft, ctx.matchesGolden("yaft-ime-off.png"));
  }

  SECTION("composing and converting") {
    tap("ctrl");
    tap(" ");
    for (const auto* key :
         { "s", "i", "d", "f", "y", "v", "s", "h", "s", "l" }) {
      tap(key);
    }
    ctx.pump();
    REQUIRE_THAT(yaft, ctx.matchesGolden("yaft-ime-composing.png"));

    // The recorded reply to this key shows the candidate window.
    tap(" ");
    ctx.pump();
    REQUIRE_THAT(yaft, ctx.matchesGolden("yaft-ime-bar.png"));
  }
}

TEST_CASE("Yaft: only the shell's exit stops it", "[yaft][ui]") {
  auto ctx = TestContext::make();
  std::string program = CAT_EXE;
  std::vector<char*> args = { program.data(), nullptr };

  // The input method helper exits right away, like a crashing mozc.
  YaftConfigAndError cfgAndErr;
  cfgAndErr.config = YaftConfig::getDefault();
  cfgAndErr.config.ime.mozcCommand = { "/bin/sh", "-c", "exit 3" };

  ctx.pumpWidget(Yaft(args.front(), args.data(), cfgAndErr));
  ctx.pump(std::chrono::milliseconds(300));
  REQUIRE_FALSE(ctx.shouldStop());

  // The bar tells that mozc is gone when converting.
  auto kbd = ctx.findByType<Keyboard>();
  const auto tap = [&](auto name) { tapKey(ctx, kbd, name, colemak_layout); };
  tap("ctrl");
  tap(" ");
  tap("r");
  tap(" ");
  ctx.pump();
  REQUIRE_FALSE(ctx.shouldStop());

  // The shell exiting still does: commit the kana, end its line, ^D.
  tap(":enter");
  tap(":enter");
  tap("ctrl");
  tap("d");
  REQUIRE(ctx.shouldStop());
}

TEST_CASE("Yaft: input method bar in landscape", "[yaft][ui]") {
  TemporaryDirectory tmp;
  const auto script = tmp.dir / "helper.sh";
  std::ofstream(script) << mozc_replay_script;

  // With the type folio attached yaft rotates and hides the keyboard.
  auto ctx = TestContext::make(/* keyboardAttached */ true);
  std::string program = CAT_EXE;
  std::vector<char*> args = { program.data(), nullptr };

  YaftConfigAndError cfgAndErr;
  cfgAndErr.config = YaftConfig::getDefault();
  cfgAndErr.config.ime.mozcCommand = {
    "/bin/sh",
    script.string(),
    (assets_path / "mozc" / "session1.out").string(),
  };

  ctx.pumpWidget(Yaft(args.front(), args.data(), cfgAndErr));
  ctx.pump(std::chrono::milliseconds(500));
  auto yaft = ctx.findByType<Yaft>();

  const auto tap = [&](int scancode) {
    for (const auto type : { SDL_KEYDOWN, SDL_KEYUP }) {
      SDL_Event ev{};
      ev.type = type;
      ev.key.keysym.scancode = SDL_Scancode(scancode);
      ctx.sendEv(ev);
    }
  };

  // Right alt switches on, then "sidfyvshsl" on a Colemak keyboard.
  tap(KEY_RIGHTALT);
  for (const int key : { KEY_D,
                         KEY_L,
                         KEY_G,
                         KEY_E,
                         KEY_O,
                         KEY_V,
                         KEY_D,
                         KEY_H,
                         KEY_D,
                         KEY_U }) {
    tap(key);
  }
  tap(KEY_SPACE);
  ctx.pump();
  REQUIRE_THAT(yaft, ctx.matchesGolden("yaft-ime-landscape.png"));
}

TEST_CASE("Yaft: input method follows the config", "[yaft][ui]") {
  TemporaryDirectory tmp;
  setenv("HOME", tmp.dir.c_str(), 1);
  setenv("XDG_CONFIG_HOME", "", 1);

  const auto path = getConfigPath();
  std::filesystem::create_directories(path.parent_path());
  const auto writeConfig = [&](std::string_view content) {
    std::ofstream(path) << content;
  };
  writeConfig("[ime]\nenabled = false\n");

  std::string program = CAT_EXE;
  std::vector<char*> args = { program.data(), nullptr };

  auto ctx = TestContext::make();
  ctx.pumpWidget(Yaft(args.front(), args.data(), path));
  ctx.pump(std::chrono::milliseconds(100));

  const auto barHeight = [&] {
    const auto bars = ctx.findByType<ImeBar>();
    REQUIRE(bars.size() == 1);
    return bars.front().ro->getSize().height;
  };

  // A disabled input method takes no space at all.
  CHECK(barHeight() == 0);

  writeConfig("[ime]\nenabled = true\nmozc-command = [\"/bin/sh\", \"-c\", "
              "\"cat > /dev/null\"]\n");
  ctx.pump(std::chrono::milliseconds(200));
  CHECK(barHeight() == ImeBar::height);

  writeConfig("[ime]\nenabled = false\n");
  ctx.pump(std::chrono::milliseconds(200));
  CHECK(barHeight() == 0);
}

TEST_CASE("IME bar: layout", "[yaft][ui]") {
  auto ctx = TestContext::make();

  ime::ImeView view;
  view.enabled = true;
  view.mode = ime::ImeView::Mode::Converting;

  SECTION("a long preedit shows its tail and keeps the right side") {
    view.preedit = {
      { "きょうはいいてんきなのでさんぽにいこうとおもいます", false },
      { "いきましょう", true },
    };
    view.candidates = { { "1", "行きましょう", "" },
                        { "2", "生きましょう", "" } };
    view.focused = 0;
    view.pageInfo = "1/12";
    view.status = "mozc 起動中";

    ctx.pumpWidget(Center(Sized(ImeBar(view, true), 900, ImeBar::height)));
    auto bar = ctx.findByType<ImeBar>();
    REQUIRE_THAT(bar, ctx.matchesGolden("ime-bar-long.png"));
  }

  SECTION("the focused candidate scrolls into view") {
    view.preedit = { { "わたし", true } };
    for (int i = 1; i <= 9; i++) {
      view.candidates.push_back(
        { std::to_string(i), "候補その" + std::to_string(i), "" });
    }
    view.focused = 8;
    view.pageInfo = "9/20";

    ctx.pumpWidget(Center(Sized(ImeBar(view, true), 700, ImeBar::height)));
    auto bar = ctx.findByType<ImeBar>();
    REQUIRE_THAT(bar, ctx.matchesGolden("ime-bar-scrolled.png"));
  }
}
