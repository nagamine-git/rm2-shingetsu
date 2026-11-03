#pragma once

#include "config.h"
#include "ime/ImeBar.h"
#include "ime/ImeSession.h"
#include "keyboard.h"
#include "layout.h"
#include "screen.h"
#include "terminal_adapter.h"

#include <UI/Rotate.h>
#include <UI/StatefulWidget.h>

#include <filesystem>
#include <memory>

class YaftState;

class Yaft : public rmlib::StatefulWidget<Yaft> {
public:
  Yaft(const char* cmd, char* const argv[], YaftConfigAndError config)
    : configOrPath(std::move(config)), cmd(cmd), argv(argv) {}

  Yaft(const char* cmd, char* const argv[], std::filesystem::path configPath)
    : configOrPath(std::move(configPath)), cmd(cmd), argv(argv) {}

  static YaftState createState();

  YaftConfigAndError getConfigAndError() const;

private:
  friend class YaftState;

  std::variant<YaftConfigAndError, std::filesystem::path> configOrPath;

  const char* cmd;
  char* const* argv;
};

class YaftState : public rmlib::StateBase<Yaft> {
public:
  ~YaftState();

  /// Logs the given string to the terminal console.
  void logTerm(std::string_view str);

  void init(rmlib::AppContext& ctx, const rmlib::BuildContext& /*unused*/);

  auto build(rmlib::AppContext& ctx,
             const rmlib::BuildContext& buildCtx) const {
    using namespace rmlib;

    const auto& layout = [this]() -> const Layout& {
      if (hideKeyboard) {
        return empty_layout;
      }

      if (smallKeyboard) {
        return hidden_layout;
      }

      return *config.layout;
    }();

    const auto repeatRateMs =
      std::chrono::milliseconds(1000) / config.repeatRate;
    return Rotated(
      rotation,
      Column(
        Expanded(Screen(term.get(), false, config.autoRefresh)),
        ImeBar(imeSession != nullptr ? imeSession->controller.view()
                                     : ime::ImeView{},
               /* visible */ imeSession != nullptr),
        Keyboard(
          term.get(),
          KeyboardParams{
            .layout = layout,
            .keymap = *config.keymap,
            .repeatDelay = std::chrono::milliseconds(config.repeatDelay),
            .repeatTime = repeatRateMs,
            .ime = imeSession != nullptr ? &imeSession->controller : nullptr,
            .imeActivity =
              [this, &ctx] {
                setState([&ctx](auto& self) { self.syncMozcFd(ctx); });
              },
          },
          [this](int num) {
            setState(
              [](auto& self) { self.smallKeyboard = !self.smallKeyboard; });
          })));
  }

private:
  void checkLandscape(rmlib::AppContext& ctx);
  void readInotify(rmlib::AppContext& ctx) const;

  // (Re)creates the input method session if the [ime] config changed.
  void applyImeConfig(rmlib::AppContext& ctx);
  // Keeps the mozc pipe registered with the event loop (it changes on respawn).
  void syncMozcFd(rmlib::AppContext& ctx);
  void readReapedChildren(rmlib::AppContext& ctx);

  std::unique_ptr<Terminal> term;
  rmlib::TimerHandle pogoTimer;

  std::filesystem::path watchPath;
  unistdpp::FD inotifyFd;
  int inotifyWd;

  rmlib::FdHandle inotifyFdHandle;
  rmlib::FdHandle termFdHandle;
  rmlib::FdHandle stdinFdHandle;

  std::unique_ptr<ime::ImeSession> imeSession;
  ImeSettings activeIme;
  int mozcFd = -1;
  rmlib::FdHandle mozcFdHandle;

  // Read end of the pipe the SIGCHLD handler reports reaped children on.
  unistdpp::FD reapedFd;
  rmlib::FdHandle reapedFdHandle;

  YaftConfig config;
  rmlib::Rotation rotation = rmlib::Rotation::None;
  bool smallKeyboard = false;
  bool hideKeyboard = false;
};
