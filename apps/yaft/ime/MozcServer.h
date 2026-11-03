#pragma once

#include <chrono>
#include <string>
#include <sys/types.h>
#include <vector>

namespace ime {

/// Processes of one mozc chroot, as seen from outside of it.
struct MozcProcesses {
  /// mozc_server owned by the given uid.
  std::vector<pid_t> servers;
  /// A mozc_emacs_helper of any owner is alive.
  bool helperRunning = false;
};

/// Scans `procDir` (normally /proc). A process only counts if its exe is the
/// binary inside `root`, so other roots and the host's own mozc are ignored.
MozcProcesses
findMozcProcesses(const std::string& procDir,
                  const std::string& root,
                  uid_t uid);

/// Stops servers left behind by an earlier helper. They live in that helper's
/// mount namespace, so mozc rejects them (/proc/<pid>/exe does not match) and
/// they keep the server lock. Does nothing while a helper is alive: another
/// instance is using its server. SIGTERM first, SIGKILL after `grace`.
void
stopStaleMozcServers(const std::string& procDir,
                     const std::string& root,
                     uid_t uid,
                     std::chrono::milliseconds grace = std::chrono::seconds(1));

} // namespace ime
