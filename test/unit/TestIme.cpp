#include <catch2/catch_test_macros.hpp>

#include "FakeConverter.h"
#include "TempFiles.h"

#include "ime/Composer.h"
#include "ime/ImeController.h"
#include "ime/MozcClient.h"
#include "ime/MozcProtocol.h"
#include "ime/Sexp.h"
#include "ime/Utf8.h"

#include <chrono>
#include <fstream>
#include <sstream>

#include <signal.h>
#include <sys/wait.h>

using namespace std::chrono_literals;

namespace {

const std::filesystem::path mozc_assets =
  std::filesystem::path(ASSETS_PATH) / "mozc";

std::vector<std::string>
readLines(const std::filesystem::path& path) {
  std::ifstream ifs(path);
  REQUIRE(ifs.good());
  std::vector<std::string> lines;
  for (std::string line; std::getline(ifs, line);) {
    lines.push_back(line);
  }
  return lines;
}

// ---------------------------------------------------------------- Composer

const ime::Composer&
colemak() {
  static const auto composer = *ime::Composer::load("colemak");
  return composer;
}

const ime::Composer&
qwerty() {
  static const auto composer = *ime::Composer::load("qwerty");
  return composer;
}

std::string
compose(const ime::Composer& base, std::string_view keys) {
  auto composer = base;
  for (const auto c : ime::decodeUtf8(keys)) {
    composer.add(c);
  }
  return composer.text();
}

// ------------------------------------------------------------- Controller

using ime::ImeKey;
using Special = ImeKey::Special;

ImeKey
ch(char c) {
  return ImeKey::character(char32_t(c));
}

ImeKey
ctrl(char c) {
  return ImeKey::character(char32_t(c), /* ctrl */ true);
}

ImeKey
named(Special special, bool shift = false) {
  return ImeKey::named(special, shift);
}

struct Harness {
  explicit Harness(bool escapeOff = true)
    : ime(*ime::Composer::load("colemak"), &conv, { escapeOff }) {}

  // Types printable keys, returns everything committed.
  std::string type(std::string_view keys) {
    std::string committed;
    for (const char c : keys) {
      const auto result = ime.handleKey(ch(c));
      CHECK_FALSE(result.passthrough);
      committed += result.commit;
    }
    return committed;
  }

  std::string text() {
    std::string out;
    for (const auto& segment : ime.view().preedit) {
      out += segment.text;
    }
    return out;
  }

  FakeConverter conv;
  ime::ImeController ime;
};

// ------------------------------------------------------------ MozcClient

constexpr std::string_view greeting =
  R"(echo '((mozc-emacs-helper . t)(version . "test")(config . ((preedit-method . roman))))')";

ime::MozcClient::Options
shOptions(const std::string& script) {
  ime::MozcClient::Options options;
  options.command = { "/bin/sh", "-c", script };
  options.replyTimeout = 300ms;
  return options;
}

std::vector<ime::MozcKey>
kanaKeys(std::string_view kana) {
  std::vector<ime::MozcKey> keys;
  for (const auto c : ime::decodeUtf8(kana)) {
    keys.push_back(ime::mozcKanaKey(ime::encodeUtf8(std::u32string(1, c))));
  }
  return keys;
}

} // namespace

TEST_CASE("Composer: shingetsu colemak table", "[ime][composer]") {
  const std::vector<std::pair<std::string, std::string>> cases = {
    { "r", "か" },
    { "ri", "が" },
    { "a", "は" },
    { "ai", "ば" },
    { "aii", "ぱ" },
    { "n", "う" },
    { "ni", "ゔ" },
    { "nii", "ぅ" },
    { "ea", "あ" },
    { "eai", "ぁ" },
    { "eq", "ゆ" },
    { "ew", "ひ" },
    { "ewi", "び" },
    { "ewii", "ぴ" },
    { "sh", "ま" },
    { "sl", "え" },
    { "sli", "ぇ" },
    { "so", "ち" },
    { "soi", "ぢ" },
    { "eiq", "ぴょ" },
    { "eig", "でぃ" },
    { "eii", ";" },
    { "ee", "も" },
    { "ss", "ら" },
    { "s;", "ー" },
    { "-", "ー" },
    { ",", "、" },
    { ".", "。" },
    { "[", "「" },
    { "]", "」" },
    { "u", "い" },
    { "ui", "ぃ" },
    { "y", "の" },
    { "k", "っ" },
    { "sidfyvshsl", "わたしのなまえ" },
  };

  for (const auto& [keys, expected] : cases) {
    INFO("keys: " << keys);
    CHECK(compose(colemak(), keys) == expected);
  }
}

TEST_CASE("Composer: shingetsu qwerty table", "[ime][composer]") {
  // In the QWERTY table the shift marks are k (☆) and d (★), and l is ゛.
  const std::vector<std::pair<std::string, std::string>> cases = {
    { "q", "そ" },  { "s", "か" },   { "sl", "が" }, { "a", "は" },
    { "al", "ば" }, { "all", "ぱ" }, { "ka", "あ" }, { "kq", "ゆ" },
    { "dh", "ま" }, { "dl", "わ" },  { "du", "え" }, { "d;", "ち" },
    { "dp", "ー" }, { "kk", "も" },  { "dd", "ら" }, { "klq", "ぴょ" },
    { "kll", ";" }, { ";", "き" },   { "j", "う" },  { "n", "っ" },
  };

  for (const auto& [keys, expected] : cases) {
    INFO("keys: " << keys);
    CHECK(compose(qwerty(), keys) == expected);
  }
}

TEST_CASE("Composer: placeholders", "[ime][composer]") {
  auto composer = colemak();
  composer.add(U'e');
  CHECK(composer.text() == "☆");
  CHECK(composer.strippedText().empty());

  composer.add(U'i');
  CHECK(composer.text() == "☆゛");
  CHECK(composer.strippedText() == "゛");

  composer.add(U'q');
  CHECK(composer.text() == "ぴょ");

  // Kana that start longer keys (か in かi) are real text, not placeholders.
  composer.clear();
  composer.add(U'r');
  CHECK(composer.strippedText() == "か");
  composer.add(U's');
  CHECK(composer.strippedText() == "か");
  CHECK(composer.text() == "か★");
}

TEST_CASE("Composer: backspace, unknown characters", "[ime][composer]") {
  auto composer = colemak();
  CHECK_FALSE(composer.backspace());

  // Backspace removes one composed character, not one keystroke.
  composer.add(U'r');
  composer.add(U'i');
  CHECK(composer.text() == "が");
  CHECK(composer.backspace());
  CHECK(composer.empty());

  for (const auto c : std::u32string_view(U"rA1")) {
    composer.add(c);
  }
  CHECK(composer.text() == "かA1");
  composer.backspace();
  CHECK(composer.text() == "かA");

  composer.clear();
  CHECK(composer.empty());
}

TEST_CASE("Composer: loading hazkey tables", "[ime][composer]") {
  SECTION("azooKey loader rules") {
    const auto composer = ime::Composer::fromTsv("q\t\tそ\r\n"
                                                 "\r\n"
                                                 "   \t \n"
                                                 "w\tこ\textra\n"
                                                 "onlyone\n"
                                                 "x\tい\n"
                                                 "x\tう\n"
                                                 "{lbracket}\t「\n"
                                                 "{rbracket}\t」\n"
                                                 "{composition-separator}\tz\n"
                                                 "ab\t{any character}\n");

    CHECK(composer.tableSize() == 5);
    CHECK(compose(composer, "q") == "そ");
    CHECK(compose(composer, "w") == "こ");
    CHECK(compose(composer, "x") == "う");
    CHECK(compose(composer, "{") == "「");
    CHECK(compose(composer, "}") == "」");
    CHECK(compose(composer, "z") == "z");
    CHECK(compose(composer, "ab") == "ab");
  }

  SECTION("longest suffix wins") {
    const auto composer = ime::Composer::fromTsv("b\tbb\n"
                                                 "ab\tAB\n"
                                                 "xab\tXAB\n");
    CHECK(compose(composer, "b") == "bb");
    CHECK(compose(composer, "cb") == "cbb");
    CHECK(compose(composer, "ab") == "AB");
    CHECK(compose(composer, "xab") == "XAB");
    CHECK(compose(composer, "yab") == "yAB");
  }

  SECTION("doubled keys after a placeholder") {
    const auto composer = ime::Composer::fromTsv("x\t☆\n"
                                                 "☆a\tあ\n"
                                                 "xx\tも\n"
                                                 "yy\tら\n");
    CHECK(compose(composer, "xa") == "あ");
    CHECK(compose(composer, "xx") == "も");
    CHECK(compose(composer, "yy") == "ら");
  }

  SECTION("an explicit key beats the generated one") {
    const auto composer = ime::Composer::fromTsv("x\t☆\n"
                                                 "☆a\tあ\n"
                                                 "☆x\tか\n"
                                                 "xx\tも\n");
    CHECK(compose(composer, "xx") == "か");
  }

  SECTION("from a file") {
    TemporaryDirectory tmp;
    const auto path = tmp.dir / "table.tsv";
    std::ofstream(path) << "z\tぞ\n";

    const auto composer = ime::Composer::load(path.string());
    REQUIRE(composer.has_value());
    CHECK(compose(*composer, "z") == "ぞ");

    std::string error;
    CHECK_FALSE(ime::Composer::load((tmp.dir / "missing").string(), &error));
    CHECK_FALSE(error.empty());
  }
}

TEST_CASE("Sexp: parsing", "[ime][mozc]") {
  const auto parse = [](std::string_view text) {
    std::string error;
    auto result = ime::Sexp::parse(text, &error);
    INFO("input: " << text << " error: " << error);
    REQUIRE(result.has_value());
    return *result;
  };

  SECTION("atoms") {
    CHECK(parse("42").number == 42);
    CHECK(parse("-3").number == -3);
    CHECK(parse("foo-bar").isSymbol("foo-bar"));
    CHECK(parse("nil").isNil());
    CHECK(parse("()").isNil());

    const auto str = parse(R"("a \"q\" \\ b わ")");
    CHECK(str.type == ime::Sexp::Type::String);
    CHECK(str.text == "a \"q\" \\ b わ");
  }

  SECTION("lists and dotted pairs") {
    const auto list = parse("(a 1 \"s\" (b))");
    REQUIRE(list.items.size() == 4);
    CHECK(list.items[1].number == 1);
    CHECK(list.items[3].items.size() == 1);

    const auto pair = parse("(key . 7)");
    REQUIRE(pair.items.size() == 1);
    REQUIRE(pair.atom() != nullptr);
    CHECK(pair.atom()->number == 7);

    // A dotted list tail is spliced in, like in Lisp.
    const auto spliced = parse("(a . (b c))");
    CHECK(spliced.items.size() == 3);
    CHECK(spliced.atom() == nullptr);
    CHECK(parse("(output . ())").items.size() == 1);
  }

  SECTION("alists") {
    const auto alist = parse("((a . 1)(b . \"x\")(c . ((d . t))))");
    REQUIRE(alist.find("a") != nullptr);
    CHECK(alist.find("a")->intOr(0) == 1);
    CHECK(alist.find("b")->stringOr() == "x");
    CHECK(alist.find("c")->find("d")->boolOr(false));
    CHECK(alist.find("missing") == nullptr);
  }

  SECTION("errors") {
    for (const char* bad :
         { "(", ")", "\"abc", "(a . )", "a b", "(a . b c)", "" }) {
      INFO("input: " << bad);
      std::string error;
      CHECK_FALSE(ime::Sexp::parse(bad, &error).has_value());
      CHECK_FALSE(error.empty());
    }
  }
}

TEST_CASE("Mozc: protocol requests", "[ime][mozc]") {
  CHECK(ime::formatCreateSession(1) == "(1 CreateSession)\n");
  CHECK(ime::formatDeleteSession(5, 2) == "(5 DeleteSession 2)\n");
  CHECK(ime::formatSendKey(2, 1, ime::mozcKanaKey("わ")) ==
        "(2 SendKey 1 97 \"わ\")\n");
  CHECK(ime::formatSendKey(9, 1, ime::mozcSymbolKey({ "space" })) ==
        "(9 SendKey 1 space)\n");
  CHECK(ime::formatSendKey(17, 1, ime::mozcSymbolKey({ "shift", "left" })) ==
        "(17 SendKey 1 shift left)\n");
  CHECK(ime::formatSendKey(16, 1, ime::mozcCodeKey('2')) ==
        "(16 SendKey 1 50)\n");
  CHECK(ime::quoteSexpString("a\"b\\c") == "\"a\\\"b\\\\c\"");

  // Our requests are the ones the real transcript was recorded with.
  const auto lines = readLines(mozc_assets / "session1.in");
  REQUIRE(lines.size() == 11);
  CHECK(lines[0] + "\n" == ime::formatCreateSession(1));
  const auto kana = kanaKeys("わたしのなまえ");
  for (size_t i = 0; i < kana.size(); i++) {
    CHECK(lines[i + 1] + "\n" ==
          ime::formatSendKey(int64_t(i) + 2, 1, kana[i]));
  }
  CHECK(lines[8] + "\n" ==
        ime::formatSendKey(9, 1, ime::mozcSymbolKey({ "space" })));
  CHECK(lines[10] + "\n" ==
        ime::formatSendKey(11, 1, ime::mozcSymbolKey({ "enter" })));
}

TEST_CASE("Mozc: transcript session 1", "[ime][mozc]") {
  const auto lines = readLines(mozc_assets / "session1.out");
  REQUIRE(lines.size() == 12);

  std::vector<ime::MozcMessage> msgs;
  for (const auto& line : lines) {
    std::string error;
    auto msg = ime::parseMozcMessage(line, &error);
    INFO("line: " << line.substr(0, 80) << " error: " << error);
    REQUIRE(msg.has_value());
    msgs.push_back(*msg);
  }

  SECTION("greeting and session") {
    CHECK(msgs[0].kind == ime::MozcMessage::Kind::Greeting);
    CHECK(msgs[0].version == "2.29.5160.102");
    CHECK(msgs[1].kind == ime::MozcMessage::Kind::Reply);
    CHECK(msgs[1].eventId == 1);
    CHECK(msgs[1].sessionId == 1);
    CHECK_FALSE(msgs[1].output.consumed);
    CHECK_FALSE(msgs[1].output.preedit.has_value());
  }

  SECTION("typing: preedit and suggestions") {
    const auto& out = msgs[2].output;
    CHECK(out.consumed);
    REQUIRE(out.preedit.has_value());
    CHECK(out.preedit->cursor == 1);
    REQUIRE(out.preedit->segments.size() == 1);
    CHECK(out.preedit->segments[0].value == "わ");
    CHECK_FALSE(out.preedit->segments[0].highlighted);

    REQUIRE(out.candidates.has_value());
    CHECK(out.candidates->category == "suggestion");
    CHECK(out.candidates->size == 1);
    CHECK(out.candidates->items[0].value == "和");
    CHECK_FALSE(out.result.has_value());

    const auto& three = *msgs[4].output.candidates;
    REQUIRE(three.items.size() == 3);
    CHECK(three.items[0].description == "ひらがな");
    CHECK(three.items[1].value == "わたしの名前");
    CHECK(three.items[1].description.empty());
  }

  SECTION("first space: segments, no candidate window") {
    const auto& out = msgs[9].output;
    REQUIRE(out.preedit.has_value());
    REQUIRE(out.preedit->segments.size() == 2);
    CHECK(out.preedit->segments[0].value == "わたしの");
    CHECK(out.preedit->segments[0].highlighted);
    CHECK(out.preedit->segments[1].value == "名前");
    CHECK_FALSE(out.preedit->segments[1].highlighted);
    CHECK(out.preedit->highlightedPosition == 0);
    CHECK(out.preedit->cursor == 6);
    CHECK_FALSE(out.candidates.has_value());
  }

  SECTION("second space: candidate window") {
    const auto& out = msgs[10].output;
    REQUIRE(out.preedit.has_value());
    CHECK(out.preedit->text() == "私の名前");

    REQUIRE(out.candidates.has_value());
    const auto& cands = *out.candidates;
    CHECK(cands.category == "conversion");
    CHECK(cands.focusedIndex == 1);
    CHECK(cands.size == 20);
    CHECK(cands.position == 0);
    CHECK(cands.pageSize == 9);
    REQUIRE(cands.items.size() == 9);
    CHECK(cands.items[0].value == "わたしの");
    CHECK(cands.items[0].description == "ひらがな");
    CHECK(cands.items[0].shortcut == "1");
    CHECK(cands.items[1].value == "私の");
    CHECK(cands.items[1].index == 1);
    CHECK(cands.items[1].shortcut == "2");
    CHECK(cands.items[8].shortcut == "9");
  }

  SECTION("enter: result") {
    const auto& out = msgs[11].output;
    CHECK(out.consumed);
    REQUIRE(out.result.has_value());
    CHECK(*out.result == "私の名前");
    CHECK_FALSE(out.preedit.has_value());
  }
}

TEST_CASE("Mozc: transcript session 2", "[ime][mozc]") {
  const auto lines = readLines(mozc_assets / "session2.out");
  REQUIRE(lines.size() == 40);

  std::vector<ime::MozcMessage> msgs;
  for (const auto& line : lines) {
    std::string error;
    auto msg = ime::parseMozcMessage(line, &error);
    INFO("line: " << line.substr(0, 80) << " error: " << error);
    REQUIRE(msg.has_value());
    msgs.push_back(*msg);
  }

  const auto segments = [&](size_t id) {
    std::string out;
    for (const auto& segment : msgs[id].output.preedit->segments) {
      out += (segment.highlighted ? "[" : "") + segment.value +
             (segment.highlighted ? "]" : "") + "|";
    }
    return out;
  };

  // Line n holds the reply to request n (line 0 is the greeting).
  CHECK(segments(13) == "[今日は]|いい天気です|");
  CHECK(segments(14) == "今日は|[いい天気です]|");      // right
  CHECK(msgs[16].output.candidates->focusedIndex == 1); // digit 2
  CHECK(segments(17) == "今日は|[いい天気で]|す|");     // shift left
  CHECK(*msgs[18].output.result == "今日はいい天気です");
  CHECK(segments(21) == "[かな]|");
  CHECK(segments(22) == "かな|");                   // escape
  CHECK_FALSE(msgs[23].output.preedit.has_value()); // escape again
  CHECK(*msgs[29].output.result == "かんじ");
  CHECK_FALSE(msgs[30].output.consumed); // escape, empty
  CHECK(msgs[37].output.candidates->focusedIndex == 2);
  CHECK(msgs[38].output.candidates->focusedIndex == 2); // digit 3
  CHECK(*msgs[39].output.result == "日本後");
}

TEST_CASE("Mozc: errors", "[ime][mozc]") {
  const auto msg = ime::parseMozcMessage(
    R"(((error . session-error)(message . "Session failed")))");
  REQUIRE(msg.has_value());
  CHECK(msg->kind == ime::MozcMessage::Kind::Error);
  CHECK(msg->error.type == "session-error");
  CHECK(msg->error.message == "Session failed");

  std::string error;
  CHECK_FALSE(ime::parseMozcMessage("not (balanced", &error).has_value());
  CHECK_FALSE(ime::parseMozcMessage("(1 2 3)", &error).has_value());
}

TEST_CASE("IME: disabled passes everything through", "[ime][controller]") {
  Harness h;
  for (const auto& key : { ch('a'),
                           ch(' '),
                           named(Special::Enter),
                           named(Special::Escape),
                           ctrl('c') }) {
    const auto result = h.ime.handleKey(key);
    CHECK(result.passthrough);
    CHECK(result.commit.empty());
    CHECK_FALSE(result.changed);
  }
  CHECK_FALSE(h.ime.enabled());
  CHECK(h.conv.calls.empty());
}

TEST_CASE("IME: toggling", "[ime][controller]") {
  Harness h;

  SECTION("Ctrl+Space") {
    auto result = h.ime.handleKey(ctrl(' '));
    CHECK_FALSE(result.passthrough);
    CHECK(result.changed);
    CHECK(h.ime.enabled());
    CHECK(h.ime.view().enabled);

    // Also while composing: the text is committed first.
    h.type("ri");
    result = h.ime.handleKey(ctrl(' '));
    CHECK(result.commit == "が");
    CHECK_FALSE(result.passthrough);
    CHECK_FALSE(h.ime.enabled());
    CHECK(h.ime.view().preedit.empty());
  }

  SECTION("on, off and toggle commands") {
    h.ime.handleCommand(ime::ImeCommand::On);
    CHECK(h.ime.enabled());
    h.ime.handleCommand(ime::ImeCommand::On);
    CHECK(h.ime.enabled());

    h.type("sidfy");
    auto result = h.ime.handleCommand(ime::ImeCommand::Off);
    CHECK(result.commit == "わたしの");
    CHECK_FALSE(h.ime.enabled());

    h.ime.handleCommand(ime::ImeCommand::Off);
    CHECK_FALSE(h.ime.enabled());

    h.ime.handleCommand(ime::ImeCommand::Toggle);
    CHECK(h.ime.enabled());
    h.ime.handleCommand(ime::ImeCommand::Toggle);
    CHECK_FALSE(h.ime.enabled());
  }

  SECTION("switching off while converting commits the conversion") {
    h.ime.handleCommand(ime::ImeCommand::On);
    h.type("ri");
    h.ime.handleKey(ch(' '));
    h.ime.handleKey(ch(' '));
    const auto result = h.ime.handleCommand(ime::ImeCommand::Off);
    CHECK(result.commit == "が#1");
    CHECK_FALSE(h.ime.enabled());
    CHECK(h.ime.view().mode == ime::ImeView::Mode::Direct);
  }
}

TEST_CASE("IME: direct mode", "[ime][controller]") {
  Harness h;
  h.ime.handleCommand(ime::ImeCommand::On);

  SECTION("printable keys start composing") {
    const auto result = h.ime.handleKey(ch('r'));
    CHECK_FALSE(result.passthrough);
    CHECK(result.changed);
    CHECK(h.ime.view().mode == ime::ImeView::Mode::Composing);
    CHECK(h.text() == "か");
  }

  SECTION("other keys pass through") {
    for (const auto& key : { ch(' '),
                             named(Special::Enter),
                             named(Special::Backspace),
                             named(Special::Tab),
                             named(Special::Left),
                             ctrl('c'),
                             ImeKey::character(U'x', false, /* alt */ true) }) {
      const auto result = h.ime.handleKey(key);
      CHECK(result.passthrough);
      CHECK(result.commit.empty());
      CHECK_FALSE(result.changed);
    }
    CHECK(h.ime.enabled());
  }

  SECTION("escape switches off") {
    const auto result = h.ime.handleKey(named(Special::Escape));
    CHECK(result.passthrough);
    CHECK_FALSE(h.ime.enabled());
  }

  SECTION("ctrl-[ is escape too") {
    const auto result = h.ime.handleKey(ctrl('['));
    CHECK(result.passthrough);
    CHECK_FALSE(h.ime.enabled());
  }
}

TEST_CASE("IME: escape-off can be disabled", "[ime][controller]") {
  Harness h(/* escapeOff */ false);
  h.ime.handleCommand(ime::ImeCommand::On);

  const auto result = h.ime.handleKey(named(Special::Escape));
  CHECK(result.passthrough);
  CHECK(h.ime.enabled());
}

TEST_CASE("IME: composing", "[ime][controller]") {
  Harness h;
  h.ime.handleCommand(ime::ImeCommand::On);

  SECTION("preedit shows placeholders") {
    h.type("e");
    CHECK(h.text() == "☆");
    h.type("a");
    CHECK(h.text() == "あ");
  }

  SECTION("shifted letters are literal") {
    h.ime.handleKey(ch('r'));
    h.ime.handleKey(ch('A'));
    CHECK(h.text() == "かA");
  }

  SECTION("enter commits the composition") {
    h.type("sidfy");
    const auto result = h.ime.handleKey(named(Special::Enter));
    CHECK(result.commit == "わたしの");
    CHECK_FALSE(result.passthrough);
    CHECK(h.ime.view().mode == ime::ImeView::Mode::Direct);
    CHECK(h.text().empty());
    CHECK(h.conv.calls.empty());
  }

  SECTION("ctrl-m and ctrl-j commit too") {
    for (const char c : { 'm', 'j' }) {
      h.type("ri");
      const auto result = h.ime.handleKey(ctrl(c));
      CHECK(result.commit == "が");
      CHECK_FALSE(result.passthrough);
    }
  }

  SECTION("a bare placeholder commits nothing") {
    h.type("e");
    const auto result = h.ime.handleKey(named(Special::Enter));
    CHECK(result.commit.empty());
    CHECK_FALSE(result.passthrough);
    CHECK(h.ime.view().mode == ime::ImeView::Mode::Direct);
  }

  SECTION("backspace and ctrl-h") {
    h.type("rn");
    CHECK(h.text() == "かう");

    auto result = h.ime.handleKey(named(Special::Backspace));
    CHECK_FALSE(result.passthrough);
    CHECK(h.text() == "か");

    result = h.ime.handleKey(ctrl('h'));
    CHECK_FALSE(result.passthrough);
    CHECK(h.ime.view().mode == ime::ImeView::Mode::Direct);
    CHECK(h.ime.view().preedit.empty());

    // Back in direct mode backspace is the terminal's again.
    CHECK(h.ime.handleKey(named(Special::Backspace)).passthrough);
  }

  SECTION("escape cancels, a second escape switches off") {
    h.type("sidfy");
    auto result = h.ime.handleKey(named(Special::Escape));
    CHECK_FALSE(result.passthrough);
    CHECK(result.commit.empty());
    CHECK(h.ime.enabled());
    CHECK(h.text().empty());

    result = h.ime.handleKey(named(Special::Escape));
    CHECK(result.passthrough);
    CHECK_FALSE(h.ime.enabled());
  }

  SECTION("ctrl-g cancels") {
    h.type("ri");
    const auto result = h.ime.handleKey(ctrl('g'));
    CHECK_FALSE(result.passthrough);
    CHECK(h.text().empty());
    CHECK(h.ime.enabled());
  }

  SECTION("other keys commit, then pass through") {
    for (const auto& key : { named(Special::Tab),
                             named(Special::Left),
                             named(Special::Home),
                             ctrl('c'),
                             ImeKey::character(U'x', false, /* alt */ true) }) {
      h.type("ri");
      const auto result = h.ime.handleKey(key);
      CHECK(result.commit == "が");
      CHECK(result.passthrough);
      CHECK(h.ime.view().mode == ime::ImeView::Mode::Direct);
    }
  }

  SECTION("space with only a placeholder does nothing") {
    h.type("e");
    const auto result = h.ime.handleKey(ch(' '));
    CHECK_FALSE(result.passthrough);
    CHECK(h.conv.calls.empty());
    CHECK(h.ime.view().mode == ime::ImeView::Mode::Composing);
  }
}

TEST_CASE("IME: converting", "[ime][controller]") {
  Harness h;
  h.ime.handleCommand(ime::ImeCommand::On);
  h.type("sidfyvshsl");
  REQUIRE(h.text() == "わたしのなまえ");

  const auto lastCall = [&] { return h.conv.calls.back(); };
  using Keys = std::vector<std::string>;

  SECTION("space sends the kana, then space") {
    const auto result = h.ime.handleKey(ch(' '));
    CHECK_FALSE(result.passthrough);
    CHECK(result.changed);
    REQUIRE(h.conv.calls.size() == 1);
    CHECK(lastCall() == Keys{ "97:わ",
                              "97:た",
                              "97:し",
                              "97:の",
                              "97:な",
                              "97:ま",
                              "97:え",
                              "space" });

    const auto view = h.ime.view();
    CHECK(view.mode == ime::ImeView::Mode::Converting);
    REQUIRE(view.preedit.size() == 1);
    CHECK(view.preedit[0].text == "わたしのなまえ");
    CHECK(view.preedit[0].highlighted);
    CHECK(view.candidates.empty());
    CHECK(view.status.empty());
  }

  SECTION("placeholders are not sent") {
    h.ime.handleKey(named(Special::Escape));
    h.type("ae");
    CHECK(h.text() == "は☆");
    h.ime.handleKey(ch(' '));
    CHECK(lastCall() == Keys{ "97:は", "space" });
  }

  SECTION("second space opens the candidate window") {
    h.ime.handleKey(ch(' '));
    h.ime.handleKey(ch(' '));
    CHECK(lastCall() == Keys{ "space" });

    const auto view = h.ime.view();
    REQUIRE(view.candidates.size() == 9);
    CHECK(view.focused == 1);
    CHECK(view.candidates[0].label == "1");
    CHECK(view.candidates[0].value == "わたしのなまえ");
    CHECK(view.candidates[0].description == "ひらがな");
    CHECK(view.candidates[1].label == "2");
    CHECK(view.pageInfo == "2/12");
    CHECK(view.preedit[0].text == "わたしのなまえ#1");
  }

  SECTION("navigation keys") {
    h.ime.handleKey(ch(' '));

    h.ime.handleKey(named(Special::Down));
    CHECK(lastCall() == Keys{ "space" });
    h.ime.handleKey(named(Special::Tab));
    CHECK(lastCall() == Keys{ "space" });
    h.ime.handleKey(named(Special::Up));
    CHECK(lastCall() == Keys{ "up" });
    h.ime.handleKey(named(Special::Left));
    CHECK(lastCall() == Keys{ "left" });
    h.ime.handleKey(named(Special::Right));
    CHECK(lastCall() == Keys{ "right" });
    h.ime.handleKey(named(Special::Left, /* shift */ true));
    CHECK(lastCall() == Keys{ "shift left" });
    h.ime.handleKey(named(Special::Right, /* shift */ true));
    CHECK(lastCall() == Keys{ "shift right" });

    CHECK(h.ime.view().mode == ime::ImeView::Mode::Converting);
  }

  SECTION("digits move the focus without committing") {
    h.ime.handleKey(ch(' '));
    h.ime.handleKey(ch(' '));

    const auto result = h.ime.handleKey(ch('4'));
    CHECK(result.commit.empty());
    CHECK_FALSE(result.passthrough);
    CHECK(lastCall() == Keys{ "52" });
    CHECK(h.ime.view().focused == 3);
    CHECK(h.ime.view().mode == ime::ImeView::Mode::Converting);

    h.ime.handleKey(ch('1'));
    CHECK(lastCall() == Keys{ "49" });
    CHECK(h.ime.view().focused == 0);
  }

  SECTION("paging") {
    for (int i = 0; i < 10; i++) {
      h.ime.handleKey(ch(' '));
    }
    const auto view = h.ime.view();
    CHECK(view.pageInfo == "10/12");
    REQUIRE(view.candidates.size() == 3);
    CHECK(view.focused == 0);
    CHECK(view.candidates[0].label == "1");
  }

  SECTION("enter commits mozc's result") {
    h.ime.handleKey(ch(' '));
    h.ime.handleKey(ch(' '));
    const auto result = h.ime.handleKey(named(Special::Enter));
    CHECK(result.commit == "わたしのなまえ#1");
    CHECK_FALSE(result.passthrough);
    CHECK(lastCall() == Keys{ "enter" });
    CHECK(h.ime.view().mode == ime::ImeView::Mode::Direct);
    CHECK(h.ime.view().preedit.empty());

    // The next composition starts clean.
    h.type("ri");
    CHECK(h.text() == "が");
  }

  SECTION("escape goes back to composing, buffer intact") {
    h.ime.handleKey(ch(' '));
    h.ime.handleKey(ch(' '));
    const auto result = h.ime.handleKey(named(Special::Escape));
    CHECK_FALSE(result.passthrough);
    CHECK(result.commit.empty());
    CHECK(lastCall() == Keys{ "escape", "escape" });

    const auto view = h.ime.view();
    CHECK(view.mode == ime::ImeView::Mode::Composing);
    CHECK(h.text() == "わたしのなまえ");
    CHECK(view.candidates.empty());
    CHECK(h.ime.enabled());

    // ...and converting again resends everything.
    h.ime.handleKey(ch(' '));
    CHECK(lastCall().size() == 8);
  }

  SECTION("backspace goes back as well") {
    h.ime.handleKey(ch(' '));
    h.ime.handleKey(named(Special::Backspace));
    CHECK(lastCall() == Keys{ "escape", "escape" });
    CHECK(h.text() == "わたしのなまえ");
    CHECK(h.ime.view().mode == ime::ImeView::Mode::Composing);
  }

  SECTION("a printable key commits and starts a new composition") {
    h.ime.handleKey(ch(' '));
    const auto result = h.ime.handleKey(ch('r'));
    CHECK(result.commit == "わたしのなまえ");
    CHECK_FALSE(result.passthrough);
    CHECK(lastCall() == Keys{ "enter" });
    CHECK(h.ime.view().mode == ime::ImeView::Mode::Composing);
    CHECK(h.text() == "か");
  }

  SECTION("other keys commit and pass through") {
    for (const auto& key : { named(Special::Home), ctrl('c') }) {
      h.ime.handleKey(ch(' '));
      const auto result = h.ime.handleKey(key);
      CHECK(result.commit == "わたしのなまえ");
      CHECK(result.passthrough);
      CHECK(lastCall() == Keys{ "enter" });
      CHECK(h.ime.view().mode == ime::ImeView::Mode::Direct);
      h.type("sidfyvshsl");
    }
  }

  SECTION("mozc dying mid-conversion falls back to composing") {
    h.ime.handleKey(ch(' '));
    h.conv.failNext = true;
    const auto result = h.ime.handleKey(ch(' '));
    CHECK_FALSE(result.passthrough);

    const auto view = h.ime.view();
    CHECK(view.mode == ime::ImeView::Mode::Composing);
    CHECK(h.text() == "わたしのなまえ");
    CHECK(view.status == "mozc 未接続");

    // The local text can still be committed.
    const auto enter = h.ime.handleKey(named(Special::Enter));
    CHECK(enter.commit == "わたしのなまえ");
    CHECK(h.ime.view().mode == ime::ImeView::Mode::Direct);
  }

  SECTION("mozc dying on commit keeps the composition") {
    h.ime.handleKey(ch(' '));
    h.conv.failNext = true;
    const auto result = h.ime.handleKey(named(Special::Enter));
    CHECK(result.commit.empty());
    CHECK(h.ime.view().mode == ime::ImeView::Mode::Composing);
    CHECK(h.text() == "わたしのなまえ");
  }

  SECTION("ctrl-space while converting") {
    h.ime.handleKey(ch(' '));
    const auto result = h.ime.handleKey(ctrl(' '));
    CHECK(result.commit == "わたしのなまえ");
    CHECK_FALSE(h.ime.enabled());
  }
}

TEST_CASE("IME: mozc not ready", "[ime][controller]") {
  Harness h;
  h.ime.handleCommand(ime::ImeCommand::On);
  h.type("ri");

  SECTION("still starting") {
    h.conv.current = ime::ConverterState::Starting;
    const auto result = h.ime.handleKey(ch(' '));
    CHECK_FALSE(result.passthrough);
    CHECK(result.changed);
    CHECK(h.conv.calls.empty());

    auto view = h.ime.view();
    CHECK(view.mode == ime::ImeView::Mode::Composing);
    CHECK(view.status == "mozc 起動中");
    CHECK(h.text() == "が");

    // The status goes away once mozc is up, and Space works.
    h.conv.current = ime::ConverterState::Ready;
    CHECK(h.ime.view().status.empty());
    h.ime.handleKey(ch(' '));
    CHECK(h.ime.view().mode == ime::ImeView::Mode::Converting);
  }

  SECTION("unavailable") {
    h.conv.current = ime::ConverterState::Unavailable;
    h.ime.handleKey(ch(' '));
    CHECK(h.ime.view().status == "mozc 未接続");
    CHECK(h.conv.restarts == 1);
    CHECK(h.text() == "が");

    // Typing on clears the message.
    h.ime.handleKey(ch('r'));
    CHECK(h.ime.view().status.empty());
  }

  SECTION("no converter at all") {
    ime::ImeController bare(*ime::Composer::load("colemak"), nullptr);
    bare.handleCommand(ime::ImeCommand::On);
    bare.handleKey(ch('r'));
    bare.handleKey(ch(' '));
    CHECK(bare.view().status == "mozc 未接続");
    CHECK(bare.handleKey(named(Special::Enter)).commit == "か");
  }

  SECTION("conversion that fails right away") {
    h.conv.failNext = true;
    h.ime.handleKey(ch(' '));
    CHECK(h.ime.view().mode == ime::ImeView::Mode::Composing);
    CHECK(h.ime.view().status == "mozc 未接続");
  }
}

TEST_CASE("MozcClient: replayed transcript", "[ime][mozc][process]") {
  TemporaryDirectory tmp;
  const auto script = tmp.dir / "helper.sh";
  std::ofstream(script) << mozc_replay_script;

  ime::MozcClient::Options options;
  options.command = { "/bin/sh",
                      script.string(),
                      (mozc_assets / "session1.out").string() };
  options.replyTimeout = 2000ms;
  ime::MozcClient client(options);

  CHECK(client.state() == ime::ConverterState::Unavailable);
  REQUIRE(client.start());
  CHECK(client.state() == ime::ConverterState::Starting);
  CHECK(client.fd() >= 0);

  // Greeting, CreateSession, warm-up key; all without blocking the caller.
  REQUIRE(client.waitReady(5000ms));
  CHECK(client.state() == ime::ConverterState::Ready);

  // Our request ids line up with the transcript's replies.
  auto keys = kanaKeys("わたしのな");
  keys.push_back(ime::mozcKanaKey("ま"));
  keys.push_back(ime::mozcKanaKey("え"));
  keys.push_back(ime::mozcSymbolKey({ "space" }));
  const auto out = client.sendKeys(keys);
  REQUIRE(out.has_value());
  REQUIRE(out->candidates.has_value());
  CHECK(out->candidates->focusedIndex == 1);
  CHECK(out->candidates->items[1].value == "私の");

  const auto commit = client.sendKeys({ ime::mozcSymbolKey({ "enter" }) });
  REQUIRE(commit.has_value());
  CHECK(commit->result == "私の名前");

  // The transcript is used up: the helper exits, which marks the client
  // unavailable, and a restart is rate limited.
  CHECK_FALSE(client.sendKeys({ ime::mozcSymbolKey({ "enter" }) }).has_value());
  CHECK(client.state() == ime::ConverterState::Unavailable);
  CHECK(client.fd() == -1);
  CHECK_FALSE(client.lastError().empty());

  client.restart();
  CHECK(client.state() == ime::ConverterState::Unavailable);
}

TEST_CASE("MozcClient: asynchronous startup", "[ime][mozc][process]") {
  // The helper takes a while to answer the warm-up key, like the first
  // key that launches mozc_server on the tablet.
  const std::string script = std::string(greeting) +
                             R"(
read a; echo '((emacs-event-id . 1)(emacs-session-id . 7)(output . ()))'
read b; sleep 0.3; echo '((emacs-event-id . 2)(emacs-session-id . 7)(output . ((consumed . nil))))'
cat > /dev/null)";
  ime::MozcClient client(shOptions(script));
  REQUIRE(client.start());

  // Nothing blocked: the client is still starting, and fd() can be polled.
  CHECK(client.state() == ime::ConverterState::Starting);
  CHECK_FALSE(client.sendKeys({ ime::mozcSymbolKey({ "space" }) }).has_value());
  CHECK(client.state() == ime::ConverterState::Starting);

  REQUIRE(client.waitReady(5000ms));
  CHECK(client.state() == ime::ConverterState::Ready);
}

TEST_CASE("MozcClient: failures", "[ime][mozc][process]") {
  SECTION("helper that is not there") {
    ime::MozcClient::Options options;
    options.command = { "/nonexistent/mozc_emacs_helper" };
    ime::MozcClient client(options);
    REQUIRE(client.start());
    CHECK_FALSE(client.waitReady(5000ms));
    CHECK(client.state() == ime::ConverterState::Unavailable);
    CHECK_FALSE(client.lastError().empty());
  }

  SECTION("helper error") {
    const std::string script = std::string(greeting) +
                               R"(
read a
echo '((error . session-error)(message . "Session failed"))')";
    ime::MozcClient client(shOptions(script));
    REQUIRE(client.start());
    CHECK_FALSE(client.waitReady(5000ms));
    CHECK(client.state() == ime::ConverterState::Unavailable);
    CHECK(client.lastError().find("session-error") != std::string::npos);
  }

  SECTION("garbage") {
    ime::MozcClient client(shOptions("echo 'not ( a greeting'"));
    REQUIRE(client.start());
    CHECK_FALSE(client.waitReady(5000ms));
    CHECK(client.state() == ime::ConverterState::Unavailable);
  }

  SECTION("reply timeout") {
    const std::string script = std::string(greeting) +
                               R"(
read a; echo '((emacs-event-id . 1)(emacs-session-id . 1)(output . ()))'
read b; echo '((emacs-event-id . 2)(emacs-session-id . 1)(output . ()))'
read c
exec sleep 10)";
    ime::MozcClient client(shOptions(script));
    REQUIRE(client.start());
    REQUIRE(client.waitReady(5000ms));

    const auto start = std::chrono::steady_clock::now();
    CHECK_FALSE(
      client.sendKeys({ ime::mozcSymbolKey({ "space" }) }).has_value());
    CHECK(std::chrono::steady_clock::now() - start < 3s);
    CHECK(client.state() == ime::ConverterState::Unavailable);
  }

  SECTION("child exit reported from outside") {
    ime::MozcClient client(shOptions("exec sleep 30"));
    REQUIRE(client.start());
    const auto pid = client.pid();
    CHECK(client.state() == ime::ConverterState::Starting);

    kill(pid, SIGKILL);
    waitpid(pid, nullptr, 0);
    client.childExited(pid);

    CHECK(client.state() == ime::ConverterState::Unavailable);
    CHECK(client.fd() == -1);
    CHECK(client.pid() == -1);
  }

  SECTION("restart after the rate limit") {
    ime::MozcClient client(shOptions("exit 1"));
    client.restart(); // never started: starts right away
    CHECK(client.pid() > 0);
    CHECK_FALSE(client.waitReady(5000ms));

    client.restart(); // too soon
    CHECK(client.pid() == -1);
  }
}

// Real mozc: needs MOZC_HELPER (and a $HOME with a .config directory, and not
// root). Run through the ctest-hidden tag by shingetsu/scripts/host-test.sh.
TEST_CASE("IME CLI with real mozc", "[.][mozc-integration]") {
  const auto* helper = getenv("MOZC_HELPER");
  if (helper == nullptr) {
    SKIP("MOZC_HELPER is not set");
  }

  const auto run = [&](const std::string& keys) {
    const std::string cmd = std::string(YAFT_IME_CLI) + " --keys '" + keys +
                            "' --mozc-command '" + helper +
                            "' --suppress_stderr 2>&1";
    FILE* pipe = popen(cmd.c_str(), "r");
    REQUIRE(pipe != nullptr);

    std::string output;
    char buf[256];
    while (fgets(buf, sizeof(buf), pipe) != nullptr) {
      output += buf;
    }
    const int status = pclose(pipe);
    INFO("output: " << output);
    CHECK(WIFEXITED(status));
    CHECK(WEXITSTATUS(status) == 0);
    return output;
  };

  SECTION("convert and commit") {
    CHECK(run("sidfyvshsl{space}{enter}") == "COMMIT\t私の名前\n");
  }

  SECTION("no conversion needed") {
    CHECK(run("ri{enter}") == "COMMIT\tが\n");
  }

  SECTION("conversion state is reported") {
    const auto output = run("sidfyvshsl{space}{space}");
    CHECK(output.rfind("PREEDIT\t", 0) == 0);
    CHECK(output.find("CANDIDATES\t") != std::string::npos);
    CHECK(output.find("私の") != std::string::npos);
  }

  SECTION("keys that mozc does not see") {
    CHECK(run("ri{tab}") == "COMMIT\tが\nPASS\ttab\n");
  }
}
