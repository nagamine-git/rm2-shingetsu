#include "YaftWidget.h"

#include "conf.h"
#include "terminal_adapter.h"
#include "util.h"

#include <Device.h>

#include <cerrno>
#include <csignal>
#include <cstring>

#include <fcntl.h>
#include <sys/inotify.h>
#include <sys/wait.h>

#include <unistdpp/file.h>

using namespace rmlib;

namespace {
const char* termName = "xterm-256color";

// State shared with the SIGCHLD handler.
AppContext* globalCtx = nullptr;
volatile sig_atomic_t shellPid = -1;
int reapedWriteFd = -1;

// Only the shell's exit stops yaft. Any other child (the mozc helper) is
// reaped here too, and its pid is reported to the main loop.
void
sigHandler(int signo) {
  if (signo != SIGCHLD) {
    return;
  }

  const int savedErrno = errno;
  pid_t pid = 0;
  while ((pid = waitpid(-1, nullptr, WNOHANG)) > 0) {
    if (pid == shellPid) {
      if (globalCtx != nullptr) {
        globalCtx->stop();
      }
    } else if (reapedWriteFd >= 0) {
      [[maybe_unused]] auto ignored = write(reapedWriteFd, &pid, sizeof(pid));
    }
  }
  errno = savedErrno;
}

// Returns the read end of the pipe reaped children are reported on.
unistdpp::FD
initSignalHandler(AppContext& ctx) {
  globalCtx = &ctx;

  int fds[2] = { -1, -1 };
  if (pipe2(fds, O_CLOEXEC | O_NONBLOCK) == 0) {
    reapedWriteFd = fds[1];
  }

  struct sigaction sigact{};
  memset(&sigact, 0, sizeof(struct sigaction));
  sigact.sa_handler = sigHandler;
  sigact.sa_flags = SA_RESTART;
  sigaction(SIGCHLD, &sigact, nullptr);
  return unistdpp::FD(fds[0]);
}

bool
forkAndExec(int* master,
            const char* cmd,
            char* const argv[],
            int lines,
            int cols) {
  pid_t pid = 0;
  struct winsize ws{};
  ws.ws_row = lines;
  ws.ws_col = cols;
  /* XXX: this variables are UNUSED (man tty_ioctl),
          but useful for calculating terminal cell size */
  ws.ws_ypixel = CELL_HEIGHT * lines;
  ws.ws_xpixel = CELL_WIDTH * cols;

  // The handler has to know the shell's pid before it can see it exit.
  sigset_t chldSet;
  sigset_t oldSet;
  sigemptyset(&chldSet);
  sigaddset(&chldSet, SIGCHLD);
  sigprocmask(SIG_BLOCK, &chldSet, &oldSet);

  pid = eforkpty(master, nullptr, &ws);
  if (pid < 0) {
    sigprocmask(SIG_SETMASK, &oldSet, nullptr);
    return false;
  }
  if (pid == 0) { /* child */
    sigprocmask(SIG_SETMASK, &oldSet, nullptr);
    setenv("TERM", termName, 1);
    execvp(cmd, argv);
    /* never reach here */
    exit(EXIT_FAILURE);
  }

  shellPid = pid;
  sigprocmask(SIG_SETMASK, &oldSet, nullptr);
  return true;
}
} // namespace

YaftConfigAndError
Yaft::getConfigAndError() const {
  if (std::holds_alternative<YaftConfigAndError>(configOrPath)) {
    return std::get<YaftConfigAndError>(configOrPath);
  }

  return loadConfigOrMakeDefault(std::get<std::filesystem::path>(configOrPath));
}

YaftState::~YaftState() {
  signal(SIGCHLD, SIG_DFL);

  if (imeSession != nullptr) {
    imeSession->client.setFdListener(nullptr);
  }

  if (reapedWriteFd >= 0) {
    close(reapedWriteFd);
    reapedWriteFd = -1;
  }
  globalCtx = nullptr;
  shellPid = -1;
}

void
YaftState::logTerm(std::string_view str) {
  term->parse(reinterpret_cast<const uint8_t*>(str.data()), str.size());
}

void
YaftState::init(rmlib::AppContext& ctx, const rmlib::BuildContext& /*unused*/) {
  // Terminal needs the maximum size to seed the initial pty size; doLayout()
  // resizes it to the real size once known.
  int maxSize = std::max(ctx.getFbCanvas().width(), ctx.getFbCanvas().height());
  term = std::make_unique<Terminal>(maxSize, maxSize);

  auto cfgAndError = getWidget().getConfigAndError();
  config = std::move(cfgAndError.config);

  if (const auto& err = cfgAndError.err; err.has_value()) {
    logTerm(err->msg);
  }

  if (std::holds_alternative<std::filesystem::path>(getWidget().configOrPath)) {
    watchPath = std::get<std::filesystem::path>(getWidget().configOrPath);
    inotifyFd = unistdpp::FD(inotify_init1(IN_CLOEXEC));
    if (inotifyFd.isValid()) {
      inotifyWd = inotify_add_watch(inotifyFd.fd,
                                    watchPath.parent_path().c_str(),
                                    IN_MODIFY | IN_CREATE | IN_CLOSE_WRITE);

      inotifyFdHandle =
        ctx.listenFd(inotifyFd.fd, [this, &ctx] { readInotify(ctx); });
    }
  }

  reapedFd = initSignalHandler(ctx);
  if (reapedFd.isValid()) {
    reapedFdHandle = ctx.listenFd(reapedFd.fd, [this, &ctx] {
      setState([&ctx](auto& self) { self.readReapedChildren(ctx); });
    });
  }

  if (!forkAndExec(&term->fd,
                   getWidget().cmd,
                   getWidget().argv,
                   term->lines,
                   term->cols)) {
    puts("Failed to fork!");
    std::exit(EXIT_FAILURE);
  }

  termFdHandle = ctx.listenFd(term->fd, [this] {
    std::array<char, 512> buf{};
    auto size = read(term->fd, buf.data(), buf.size());

    // Only update if the buffer isn't full. Otherwise more data is comming
    // probably.
    if (size != int(buf.size())) {
      setState([&](auto& self) {
        self.term->parse(reinterpret_cast<uint8_t*>(buf.data()), size);
      });
    } else {
      term->parse(reinterpret_cast<uint8_t*>(buf.data()), size);
    }
  });

  // listen to stdin in debug.
  if constexpr (USE_STDIN != 0U) {
    stdinFdHandle = ctx.listenFd(STDIN_FILENO, [this] {
      std::array<char, 512> buf{};
      auto size = read(STDIN_FILENO, buf.data(), buf.size());
      if (size > 0) {
        term->write(reinterpret_cast<const uint8_t*>(buf.data()), size);
      }
    });
  }

  checkLandscape(ctx);
  ctx.onDeviceUpdate([this, &ctx] { modify().checkLandscape(ctx); });

  applyImeConfig(ctx);
}

void
YaftState::applyImeConfig(rmlib::AppContext& ctx) {
  if (activeIme == config.ime &&
      (imeSession != nullptr) == config.ime.enabled) {
    return;
  }

  mozcFdHandle.disable();
  mozcFd = -1;
  if (imeSession != nullptr) {
    imeSession->client.setFdListener(nullptr);

    // The keyboard still points at it until the next rebuild.
    std::shared_ptr<ime::ImeSession> retired = std::move(imeSession);
    ctx.doLater([retired] {});
  }

  activeIme = config.ime;
  if (!config.ime.enabled) {
    return;
  }

  std::string error;
  auto composer = ime::Composer::load(config.ime.table, &error);
  if (!composer.has_value()) {
    logTerm("ime: " + error + ", using the colemak table\r\n");
    composer = ime::Composer::load("colemak");
  }

  ime::MozcClient::Options mozc;
  mozc.root = config.ime.mozcRoot;
  mozc.uid = config.ime.mozcUid;
  mozc.command = config.ime.mozcCommand;

  imeSession = std::make_unique<ime::ImeSession>(
    std::move(*composer),
    std::move(mozc),
    ime::ImeConfig{ .escapeOff = config.ime.escapeOff });

  // The pipe changes whenever the helper is (re)started or dies.
  imeSession->client.setFdListener([this, &ctx] { syncMozcFd(ctx); });

  // Spawning never waits for mozc: the handshake continues once the fd is
  // readable, and Space reports "not ready" until then.
  if (!imeSession->client.start()) {
    logTerm("ime: cannot start mozc: " + imeSession->client.lastError() +
            "\r\n");
  }
  syncMozcFd(ctx);
}

void
YaftState::syncMozcFd(rmlib::AppContext& ctx) {
  const int fd = imeSession != nullptr ? imeSession->client.fd() : -1;
  if (fd == mozcFd) {
    return;
  }

  mozcFdHandle.disable();
  mozcFd = fd;
  if (fd >= 0) {
    mozcFdHandle = ctx.listenFd(fd, [this, &ctx] {
      imeSession->client.onReadable();
      setState([&ctx](auto& self) { self.syncMozcFd(ctx); });
    });
  }
}

void
YaftState::readReapedChildren(rmlib::AppContext& ctx) {
  pid_t pid = 0;
  while (read(reapedFd.fd, &pid, sizeof(pid)) == sizeof(pid)) {
    if (imeSession != nullptr) {
      imeSession->client.childExited(pid);
    }
  }
  syncMozcFd(ctx);
}

void
YaftState::checkLandscape(rmlib::AppContext& ctx) {
  if (config.autoRotate) {
    const auto hasKeyboard =
      ctx.getInputManager().getBaseDevices().pogoKeyboard != nullptr;
    hideKeyboard = hasKeyboard;
    rotation = hasKeyboard ? Rotation::Clockwise : config.rotation;
  } else {
    rotation = config.rotation;
  }
}

void
YaftState::readInotify(rmlib::AppContext& ctx) const {
  union {
    std::array<char, 4096> buf;
    inotify_event ev;
  } evUnion;

  auto read = unistdpp::read(inotifyFd, &evUnion, sizeof(evUnion));
  if (!read.has_value()) {
    std::cerr << "inotify read failed: " << to_string(read.error()) << "\n";
    return;
  }

  bool shouldReload = false;
  const inotify_event* event;
  for (char* ptr = evUnion.buf.begin(); ptr < evUnion.buf.begin() + *read;
       ptr += sizeof(struct inotify_event) + event->len) {
    event = (const struct inotify_event*)ptr;

    if (event->wd == inotifyWd && event->name == watchPath.filename()) {
      shouldReload = true;
    }
  }

  if (!shouldReload) {
    return;
  }

  std::cerr << "inotify: Config updated, reloading\n";
  setState([&](auto& self) {
    auto cfgAndErr = loadConfig(self.watchPath);
    if (cfgAndErr.err.has_value()) {
      self.logTerm(cfgAndErr.err->msg);
    }

    self.config = cfgAndErr.config;
    self.checkLandscape(ctx);
    self.applyImeConfig(ctx);
  });
}

YaftState
Yaft::createState() {
  return {};
}
