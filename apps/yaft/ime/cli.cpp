// Headless driver for the input method: feeds keys to the controller (and
// real mozc) and prints what would be typed into the terminal.
#include "ImeSession.h"
#include "Utf8.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <map>

namespace {

using ime::ImeKey;
using Special = ImeKey::Special;

struct Token {
  enum class Kind { Key, Command } kind = Kind::Key;
  ImeKey key;
  ime::ImeCommand command = ime::ImeCommand::On;
};

const std::map<std::string, Token>&
namedTokens() {
  static const auto tokens = [] {
    const auto key = [](ImeKey k) { return Token{ Token::Kind::Key, k, {} }; };
    const auto command = [](ime::ImeCommand c) {
      return Token{ Token::Kind::Command, {}, c };
    };
    return std::map<std::string, Token>{
      { "space", key(ImeKey::character(U' ')) },
      { "enter", key(ImeKey::named(Special::Enter)) },
      { "bs", key(ImeKey::named(Special::Backspace)) },
      { "esc", key(ImeKey::named(Special::Escape)) },
      { "left", key(ImeKey::named(Special::Left)) },
      { "right", key(ImeKey::named(Special::Right)) },
      { "up", key(ImeKey::named(Special::Up)) },
      { "down", key(ImeKey::named(Special::Down)) },
      { "shift-left", key(ImeKey::named(Special::Left, /* shift */ true)) },
      { "shift-right", key(ImeKey::named(Special::Right, /* shift */ true)) },
      { "tab", key(ImeKey::named(Special::Tab)) },
      { "on", command(ime::ImeCommand::On) },
      { "off", command(ime::ImeCommand::Off) },
      { "toggle", command(ime::ImeCommand::Toggle) },
    };
  }();
  return tokens;
}

bool
parseKeys(const std::string& str, std::vector<Token>& out) {
  const auto chars = ime::decodeUtf8(str);
  for (size_t i = 0; i < chars.size(); i++) {
    if (chars[i] == U'{') {
      const auto end = chars.find(U'}', i);
      if (end != std::u32string::npos) {
        const auto name = ime::encodeUtf8(chars.substr(i + 1, end - i - 1));
        const auto it = namedTokens().find(name);
        if (it == namedTokens().end()) {
          std::cerr << "unknown key token {" << name << "}\n";
          return false;
        }
        out.push_back(it->second);
        i = end;
        continue;
      }
    }
    out.push_back({ Token::Kind::Key, ImeKey::character(chars[i]), {} });
  }
  return true;
}

std::string
describe(const ImeKey& key) {
  switch (key.special) {
    case Special::None:
      return ime::encodeUtf8(std::u32string(1, key.ch));
    case Special::Enter:
      return "enter";
    case Special::Backspace:
      return "backspace";
    case Special::Escape:
      return "escape";
    case Special::Tab:
      return "tab";
    case Special::Left:
      return key.shift ? "shift-left" : "left";
    case Special::Right:
      return key.shift ? "shift-right" : "right";
    case Special::Up:
      return "up";
    case Special::Down:
      return "down";
    default:
      return "other";
  }
}

void
usage() {
  std::cerr << "usage: yaft-ime-cli [--table colemak|qwerty|FILE]\n"
               "                    [--mozc-root DIR] [--mozc-uid N]\n"
               "                    --keys STRING [--mozc-command ARGV...]\n"
               "STRING: literal characters and {space} {enter} {bs} {esc}\n"
               "        {left} {right} {up} {down} {shift-left} {shift-right}\n"
               "        {tab} {on} {off} {toggle}\n";
}

} // namespace

int
main(int argc, char* argv[]) {
  std::string table = "colemak";
  std::string keys;
  bool haveKeys = false;
  ime::MozcClient::Options mozc;
  mozc.root = "/home/root/.local/share/rm2-shingetsu/mozc-root";

  for (int i = 1; i < argc; i++) {
    const std::string arg = argv[i];
    const auto value = [&]() -> const char* {
      if (i + 1 >= argc) {
        usage();
        std::exit(2);
      }
      return argv[++i];
    };

    if (arg == "--table") {
      table = value();
    } else if (arg == "--mozc-root") {
      mozc.root = value();
    } else if (arg == "--mozc-uid") {
      mozc.uid = std::atoi(value());
    } else if (arg == "--keys") {
      keys = value();
      haveKeys = true;
    } else if (arg == "--mozc-command") {
      for (i++; i < argc; i++) {
        mozc.command.emplace_back(argv[i]);
      }
    } else {
      usage();
      return 2;
    }
  }
  if (!haveKeys) {
    usage();
    return 2;
  }

  std::vector<Token> tokens;
  if (!parseKeys(keys, tokens)) {
    return 2;
  }

  std::string error;
  auto composer = ime::Composer::load(table, &error);
  if (!composer.has_value()) {
    std::cerr << error << "\n";
    return 2;
  }

  ime::ImeSession session(std::move(*composer), std::move(mozc));
  auto& controller = session.controller;
  int status = 0;

  if (!session.client.start()) {
    std::cerr << "cannot start mozc: " << session.client.lastError() << "\n";
    status = 1;
  } else if (!session.client.waitReady(std::chrono::seconds(60))) {
    std::cerr << "mozc not ready: " << session.client.lastError() << "\n";
    status = 1;
  }

  controller.handleCommand(ime::ImeCommand::On);

  for (const auto& token : tokens) {
    ime::ImeResult result;
    if (token.kind == Token::Kind::Command) {
      result = controller.handleCommand(token.command);
    } else {
      result = controller.handleKey(token.key);
    }

    if (!result.commit.empty()) {
      std::cout << "COMMIT\t" << result.commit << "\n";
    }
    if (result.passthrough) {
      std::cout << "PASS\t" << describe(token.key) << "\n";
    }
  }

  const auto view = controller.view();
  std::string preedit;
  for (const auto& segment : view.preedit) {
    preedit += segment.text;
  }
  if (!preedit.empty()) {
    std::cout << "PREEDIT\t" << preedit << "\n";
  }
  if (!view.candidates.empty()) {
    std::cout << "CANDIDATES\t";
    for (size_t i = 0; i < view.candidates.size(); i++) {
      std::cout << (i == 0 ? "" : "|") << view.candidates[i].value;
    }
    std::cout << "\n";
  }

  if (session.client.state() == ime::ConverterState::Unavailable) {
    std::cerr << "mozc failed: " << session.client.lastError() << "\n";
    status = 1;
  } else if (!view.status.empty()) {
    std::cerr << view.status << "\n";
    status = 1;
  }
  return status;
}
