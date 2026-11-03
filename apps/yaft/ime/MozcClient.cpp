#include "MozcClient.h"

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstring>

#include <fcntl.h>
#include <grp.h>
#include <poll.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef __linux__
#include <sched.h>
#include <sys/mount.h>
#endif

namespace ime {

namespace {

using namespace std::chrono_literals;

// The first key makes the helper launch mozc_server, which is slow on the
// tablet, but it must not hang forever either.
constexpr auto startup_timeout = 60s;
constexpr auto respawn_interval = 5s;

// Replies are a few KiB; a helper that sends more than this without us
// consuming it is broken or hostile.
constexpr size_t max_inbuf = 1 << 20;

constexpr const char* helper_path = "/usr/bin/mozc_emacs_helper";

// The child only uses async-signal-safe calls, so report errors by hand.
[[noreturn]] void
childFail(const char* stage) {
  char digits[16];
  int len = 0;
  for (int e = errno; e > 0 && len < 15; e /= 10) {
    digits[len++] = char('0' + e % 10);
  }
  std::reverse(digits, digits + len);

  const auto put = [](const char* str) {
    size_t n = 0;
    while (str[n] != '\0') {
      n++;
    }
    [[maybe_unused]] auto ignored = ::write(STDERR_FILENO, str, n);
  };
  put("mozc chroot: ");
  put(stage);
  put(" failed, errno ");
  [[maybe_unused]] auto ignored = ::write(STDERR_FILENO, digits, len);
  put("\n");
  _exit(127);
}

} // namespace

MozcClient::MozcClient(Options options) : options(std::move(options)) {}

MozcClient::~MozcClient() {
  if (phase == Phase::Ready && session >= 0) {
    send(formatDeleteSession(nextEventId++, session));
  }
  markDead("shutting down");
}

bool
MozcClient::start() {
  lastSpawn = Clock::now();
  everStarted = true;

  // A helper that died must show up as EPIPE, not kill the whole app.
  std::signal(SIGPIPE, SIG_IGN);

  markDead("restarting");
  error.clear();
  return spawn();
}

void
MozcClient::runChrootChild(const ChrootPlan& plan, int stdinFd, int stdoutFd) {
#ifdef __linux__
  const char* const argv[] = { "mozc_emacs_helper",
                               "--suppress_stderr",
                               nullptr };
  const char* const envp[] = {
    "HOME=/home/mozc", "PATH=/usr/bin:/bin", "LANG=C.UTF-8", nullptr
  };

  if (dup2(stdinFd, STDIN_FILENO) < 0 || dup2(stdoutFd, STDOUT_FILENO) < 0) {
    childFail("dup2");
  }
  // Don't leak the pty, framebuffer, ... into the helper.
  for (int fd = 3; fd < 4096; fd++) {
    close(fd);
  }

  if (unshare(CLONE_NEWNS) != 0) {
    childFail("unshare");
  }
  if (mount(nullptr, "/", nullptr, MS_REC | MS_PRIVATE, nullptr) != 0) {
    childFail("make / private");
  }
  if (mount("proc", plan.procDir.c_str(), "proc", 0, nullptr) != 0) {
    childFail("mount proc");
  }
  if (mount("/dev", plan.devDir.c_str(), nullptr, MS_BIND | MS_REC, nullptr) !=
      0) {
    childFail("bind /dev");
  }
  if (chroot(plan.root.c_str()) != 0 || chdir("/") != 0) {
    childFail("chroot");
  }
  if (setgroups(0, nullptr) != 0 || setgid(plan.id) != 0 ||
      setuid(plan.id) != 0) {
    childFail("drop privileges");
  }
  // The server wants $HOME/.config, but only creates $HOME/.config/mozc.
  if (mkdir("/home/mozc/.config", 0700) != 0 && errno != EEXIST) {
    childFail("mkdir .config");
  }
  execve(helper_path,
         const_cast<char* const*>(argv),
         const_cast<char* const*>(envp));
  childFail("exec helper");
#else
  (void)plan;
  (void)stdinFd;
  (void)stdoutFd;
  _exit(127);
#endif
}

bool
MozcClient::spawn() {
  int toChild[2];
  int fromChild[2];
  if (pipe2(toChild, O_CLOEXEC) != 0) {
    error = "pipe failed";
    return false;
  }
  if (pipe2(fromChild, O_CLOEXEC) != 0) {
    close(toChild[0]);
    close(toChild[1]);
    error = "pipe failed";
    return false;
  }

  std::vector<char*> argv;
  for (auto& arg : options.command) {
    argv.push_back(arg.data());
  }
  argv.push_back(nullptr);
  const bool chrootMode = options.command.empty();
  // Built before fork(): the child must not allocate.
  const ChrootPlan plan{ options.root,
                         options.root + "/proc",
                         options.root + "/dev",
                         static_cast<unsigned>(options.uid) };

  const pid_t pid = fork();
  if (pid < 0) {
    error = std::string("fork failed: ") + strerror(errno);
    close(toChild[0]);
    close(toChild[1]);
    close(fromChild[0]);
    close(fromChild[1]);
    return false;
  }

  if (pid == 0) {
    if (chrootMode) {
      runChrootChild(plan, toChild[0], fromChild[1]);
    }
    if (dup2(toChild[0], STDIN_FILENO) < 0 ||
        dup2(fromChild[1], STDOUT_FILENO) < 0) {
      _exit(127);
    }
    execvp(argv[0], argv.data());
    _exit(127);
  }

  close(toChild[0]);
  close(fromChild[1]);
  inFd = toChild[1];
  outFd = fromChild[0];
  fcntl(outFd, F_SETFL, fcntl(outFd, F_GETFL) | O_NONBLOCK);

  child = pid;
  phase = Phase::WaitGreeting;
  startDeadline = Clock::now() + startup_timeout;
  inbuf.clear();
  nextEventId = 1;
  session = -1;
  if (fdListener) {
    fdListener();
  }
  return true;
}

void
MozcClient::closePipes() {
  if (inFd >= 0) {
    close(inFd);
    inFd = -1;
  }
  if (outFd >= 0) {
    close(outFd);
    outFd = -1;
    if (fdListener) {
      fdListener();
    }
  }
}

void
MozcClient::markDead(const std::string& reason) {
  if (phase != Phase::Dead || child > 0) {
    error = reason;
  }
  phase = Phase::Dead;
  closePipes();

  if (child > 0) {
    kill(child, SIGKILL);
    // May already have been reaped by the SIGCHLD handler.
    waitpid(child, nullptr, 0);
    child = -1;
  }
}

void
MozcClient::childExited(pid_t pid) {
  if (pid != child) {
    return;
  }
  child = -1;
  markDead("helper exited");
}

ConverterState
MozcClient::state() {
  switch (phase) {
    case Phase::Ready:
      return ConverterState::Ready;
    case Phase::Dead:
      return ConverterState::Unavailable;
    default:
      if (Clock::now() > startDeadline) {
        markDead("startup timed out");
        return ConverterState::Unavailable;
      }
      return ConverterState::Starting;
  }
}

void
MozcClient::restart() {
  if (phase != Phase::Dead) {
    return;
  }
  if (everStarted && Clock::now() - lastSpawn < respawn_interval) {
    return;
  }
  start();
}

bool
MozcClient::send(const std::string& line) {
  if (inFd < 0) {
    return false;
  }
  size_t done = 0;
  while (done < line.size()) {
    const auto n = write(inFd, line.data() + done, line.size() - done);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    done += size_t(n);
  }
  return true;
}

bool
MozcClient::fill() {
  while (true) {
    char buf[4096];
    const auto n = read(outFd, buf, sizeof(buf));
    if (n > 0) {
      inbuf.append(buf, size_t(n));
      if (inbuf.size() > max_inbuf) {
        markDead("helper output too long");
        return false;
      }
      continue;
    }
    if (n == 0) {
      return false;
    }
    if (errno == EINTR) {
      continue;
    }
    return errno == EAGAIN || errno == EWOULDBLOCK;
  }
}

std::optional<std::string>
MozcClient::takeLine() {
  const auto end = inbuf.find('\n');
  if (end == std::string::npos) {
    return std::nullopt;
  }
  auto line = inbuf.substr(0, end);
  inbuf.erase(0, end + 1);
  return line;
}

std::optional<std::string>
MozcClient::readLine(Clock::time_point deadline) {
  while (true) {
    if (auto line = takeLine(); line.has_value()) {
      return line;
    }

    const auto remaining =
      std::chrono::duration_cast<std::chrono::milliseconds>(deadline -
                                                            Clock::now());
    if (remaining.count() <= 0) {
      markDead("timed out waiting for mozc");
      return std::nullopt;
    }

    pollfd pfd{ outFd, POLLIN, 0 };
    const auto ready = poll(&pfd, 1, int(remaining.count()));
    if (ready < 0 && errno == EINTR) {
      continue;
    }
    if (ready <= 0) {
      markDead("timed out waiting for mozc");
      return std::nullopt;
    }
    if (!fill()) {
      markDead("helper closed its output");
      return std::nullopt;
    }
  }
}

void
MozcClient::handleStartupLine(const std::string& line) {
  std::string parseError;
  const auto msg = parseMozcMessage(line, &parseError);
  if (!msg.has_value()) {
    markDead("bad helper output: " + parseError);
    return;
  }
  if (msg->kind == MozcMessage::Kind::Error) {
    markDead("helper error: " + msg->error.type + " " + msg->error.message);
    return;
  }

  switch (phase) {
    case Phase::WaitGreeting:
      if (msg->kind != MozcMessage::Kind::Greeting) {
        markDead("no greeting from helper");
        return;
      }
      phase = Phase::WaitSession;
      if (!send(formatCreateSession(nextEventId++))) {
        markDead("write to helper failed");
      }
      break;

    case Phase::WaitSession:
      if (msg->kind != MozcMessage::Kind::Reply || msg->sessionId < 0) {
        markDead("no session from helper");
        return;
      }
      session = msg->sessionId;
      phase = Phase::WaitWarmup;
      if (!send(formatSendKey(
            nextEventId++, session, mozcSymbolKey({ "escape" })))) {
        markDead("write to helper failed");
      }
      break;

    case Phase::WaitWarmup:
      if (msg->kind == MozcMessage::Kind::Reply) {
        phase = Phase::Ready;
      }
      break;

    default:
      break;
  }
}

void
MozcClient::onReadable() {
  if (outFd < 0) {
    return;
  }

  const bool alive = fill();
  while (phase != Phase::Dead) {
    auto line = takeLine();
    if (!line.has_value()) {
      break;
    }
    // Nothing is expected while idle; drop stray output.
    if (phase != Phase::Ready) {
      handleStartupLine(*line);
    }
  }

  if (!alive && phase != Phase::Dead) {
    markDead("helper closed its output");
  }
}

bool
MozcClient::waitReady(std::chrono::milliseconds timeout) {
  const auto deadline = Clock::now() + timeout;
  while (state() == ConverterState::Starting) {
    const auto remaining =
      std::chrono::duration_cast<std::chrono::milliseconds>(deadline -
                                                            Clock::now());
    if (remaining.count() <= 0) {
      return false;
    }
    pollfd pfd{ outFd, POLLIN, 0 };
    if (poll(&pfd, 1, int(remaining.count())) > 0) {
      onReadable();
    }
  }
  return phase == Phase::Ready;
}

std::optional<MozcOutput>
MozcClient::sendKeys(const std::vector<MozcKey>& keys) {
  if (phase != Phase::Ready) {
    return std::nullopt;
  }
  inbuf.clear();

  // Requests are tiny, so queue them all before reading any reply.
  std::vector<int64_t> ids;
  for (const auto& key : keys) {
    ids.push_back(nextEventId);
    if (!send(formatSendKey(nextEventId++, session, key))) {
      markDead("write to helper failed");
      return std::nullopt;
    }
  }

  std::optional<MozcOutput> last;
  for (const auto id : ids) {
    while (true) {
      const auto line = readLine(Clock::now() + options.replyTimeout);
      if (!line.has_value()) {
        return std::nullopt;
      }

      std::string parseError;
      const auto msg = parseMozcMessage(*line, &parseError);
      if (!msg.has_value()) {
        markDead("bad helper output: " + parseError);
        return std::nullopt;
      }
      if (msg->kind == MozcMessage::Kind::Error) {
        markDead("helper error: " + msg->error.type + " " + msg->error.message);
        return std::nullopt;
      }
      if (msg->kind == MozcMessage::Kind::Reply && msg->eventId == id) {
        last = msg->output;
        break;
      }
    }
  }
  return last;
}

} // namespace ime
