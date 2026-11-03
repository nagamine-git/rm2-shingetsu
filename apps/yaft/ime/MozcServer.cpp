#include "MozcServer.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <optional>

#include <dirent.h>
#include <poll.h>
#include <unistd.h>

namespace ime {

namespace {

using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

constexpr auto poll_interval = 25ms;
// A SIGKILLed server is gone almost at once; this only bounds a stuck one.
constexpr auto kill_wait = 500ms;

constexpr const char* server_suffix = "/usr/lib/mozc/mozc_server";
constexpr const char* helper_suffix = "/usr/bin/mozc_emacs_helper";
constexpr const char* deleted_suffix = " (deleted)";

std::optional<std::string>
readExe(const std::string& path) {
  char buf[PATH_MAX];
  const auto n = readlink(path.c_str(), buf, sizeof(buf));
  if (n <= 0) {
    return std::nullopt;
  }
  std::string exe(buf, size_t(n));

  // The kernel appends this when the binary was replaced while running.
  const std::string suffix = deleted_suffix;
  if (exe.size() > suffix.size() &&
      exe.compare(exe.size() - suffix.size(), suffix.size(), suffix) == 0) {
    exe.resize(exe.size() - suffix.size());
  }
  return exe;
}

// The real uid, first field of the Uid: line.
std::optional<uid_t>
readOwner(const std::string& path) {
  std::ifstream ifs(path);
  for (std::string line; std::getline(ifs, line);) {
    if (line.compare(0, 4, "Uid:") != 0) {
      continue;
    }
    char* end = nullptr;
    const auto uid = std::strtoul(line.c_str() + 4, &end, 10);
    if (end == line.c_str() + 4) {
      return std::nullopt;
    }
    return uid_t(uid);
  }
  return std::nullopt;
}

} // namespace

MozcProcesses
findMozcProcesses(const std::string& procDir,
                  const std::string& root,
                  uid_t uid) {
  MozcProcesses found;

  char* resolved = realpath(root.c_str(), nullptr);
  if (resolved == nullptr) {
    return found;
  }
  std::string base = resolved;
  free(resolved);
  while (!base.empty() && base.back() == '/') {
    base.pop_back();
  }
  const auto serverExe = base + server_suffix;
  const auto helperExe = base + helper_suffix;

  DIR* dir = opendir(procDir.c_str());
  if (dir == nullptr) {
    return found;
  }
  while (const dirent* entry = readdir(dir)) {
    char* end = nullptr;
    const auto pid = std::strtol(entry->d_name, &end, 10);
    if (!std::isdigit(static_cast<unsigned char>(entry->d_name[0])) ||
        *end != '\0' || pid <= 0) {
      continue;
    }

    const auto procPath = procDir + "/" + entry->d_name;
    const auto exe = readExe(procPath + "/exe");
    if (!exe.has_value()) {
      continue;
    }

    if (*exe == helperExe) {
      found.helperRunning = true;
    } else if (*exe == serverExe && pid_t(pid) != getpid()) {
      const auto owner = readOwner(procPath + "/status");
      if (owner.has_value() && *owner == uid) {
        found.servers.push_back(pid_t(pid));
      }
    }
  }
  closedir(dir);

  std::sort(found.servers.begin(), found.servers.end());
  return found;
}

void
stopStaleMozcServers(const std::string& procDir,
                     const std::string& root,
                     uid_t uid,
                     std::chrono::milliseconds grace) {
  const auto find = [&] { return findMozcProcesses(procDir, root, uid); };

  const auto first = find();
  if (first.servers.empty() || first.helperRunning) {
    return;
  }
  for (const auto pid : first.servers) {
    kill(pid, SIGTERM);
  }

  // Looking again, instead of trusting the first list, means a recycled pid
  // is never signalled.
  const auto waitGone = [&](std::chrono::milliseconds timeout) {
    const auto deadline = Clock::now() + timeout;
    auto left = find().servers;
    while (!left.empty() && Clock::now() < deadline) {
      poll(nullptr, 0, int(poll_interval.count()));
      left = find().servers;
    }
    return left;
  };

  const auto stubborn = waitGone(grace);
  for (const auto pid : stubborn) {
    kill(pid, SIGKILL);
  }
  if (!stubborn.empty()) {
    waitGone(kill_wait);
  }
}

} // namespace ime
