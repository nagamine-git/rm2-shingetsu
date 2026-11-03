#pragma once

#include "Converter.h"

#include <chrono>
#include <functional>
#include <string>
#include <sys/types.h>
#include <vector>

namespace ime {

/// Talks to mozc_emacs_helper over pipes. Startup never blocks, it advances
/// from onReadable(); once ready, sendKeys() is synchronous with a timeout.
class MozcClient : public Converter {
public:
  struct Options {
    // Production: chroot into `root` and run the helper there as `uid`.
    std::string root;
    int uid = 1000;

    // If non-empty, run this command as-is instead (host tests, ssh, ...).
    std::vector<std::string> command;

    // How long a key may take once the helper is up.
    std::chrono::milliseconds replyTimeout{ 3000 };
  };

  using Clock = std::chrono::steady_clock;

  explicit MozcClient(Options options);
  ~MozcClient() override;

  MozcClient(const MozcClient&) = delete;
  MozcClient& operator=(const MozcClient&) = delete;

  /// Spawns the helper. Returns false if the process could not be created.
  bool start();

  /// Read end of the helper's stdout for polling, -1 when not running.
  int fd() const { return outFd; }

  /// Called whenever fd() changes (spawn, helper death), including from
  /// inside state() and sendKeys().
  void setFdListener(std::function<void()> listener) {
    fdListener = std::move(listener);
  }

  /// Handles data (or EOF) on fd(); drives the asynchronous startup.
  void onReadable();

  /// Tells the client that SIGCHLD reaped `pid`.
  void childExited(pid_t pid);

  /// Blocks until ready or failed; for the CLI and tests.
  bool waitReady(std::chrono::milliseconds timeout);

  const std::string& lastError() const { return error; }
  pid_t pid() const { return child; }

  ConverterState state() override;
  void restart() override;
  std::optional<MozcOutput> sendKeys(const std::vector<MozcKey>& keys) override;

private:
  enum class Phase { Dead, WaitGreeting, WaitSession, WaitWarmup, Ready };

  struct ChrootPlan {
    std::string root;
    std::string procDir;
    std::string devDir;
    unsigned id;
  };

  bool spawn();
  [[noreturn]] static void runChrootChild(const ChrootPlan& plan,
                                          int stdinFd,
                                          int stdoutFd);
  void markDead(const std::string& reason);
  void closePipes();

  bool send(const std::string& line);
  // Reads whatever is available; false on EOF or error.
  bool fill();
  std::optional<std::string> takeLine();
  std::optional<std::string> readLine(Clock::time_point deadline);
  void handleStartupLine(const std::string& line);

  Options options;

  Phase phase = Phase::Dead;
  pid_t child = -1;
  int inFd = -1;
  int outFd = -1;

  std::function<void()> fdListener;

  std::string inbuf;
  std::string error;

  int64_t nextEventId = 1;
  int64_t session = -1;

  Clock::time_point lastSpawn{};
  Clock::time_point startDeadline{};
  bool everStarted = false;
};

} // namespace ime
