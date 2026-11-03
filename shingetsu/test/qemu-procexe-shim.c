/*
 * TEST ONLY -- never ship this in the mozc root tarball or the bundle.
 *
 * Under qemu-user, readlink("/proc/<pid>/exe") of another emulated process
 * returns the qemu binary instead of the emulated program, so mozc's IPC
 * client rejects the server it just started. Real hardware is not affected.
 *
 * This LD_PRELOAD library rewrites such results: when the target of
 * /proc/<pid>/exe is a qemu binary, it returns the emulated program's path,
 * which is the first non-option word after the interpreter in
 * /proc/<pid>/cmdline (binfmt_misc runs "qemu-arm <program> <argv0> ...").
 */
#define _GNU_SOURCE
#include <ctype.h>
#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

static ssize_t raw_readlinkat(int fd, const char *path, char *buf, size_t n) {
  return syscall(SYS_readlinkat, fd, path, buf, n);
}

/* Copies the pid out of "/proc/<digits>/<leaf>". */
static int proc_pid(const char *path, const char* leaf, char *pid, size_t n) {
  if (strncmp(path, "/proc/", 6) != 0) {
    return 0;
  }
  const char *p = path + 6;
  size_t len = 0;
  while (isdigit((unsigned char)p[len])) {
    len++;
  }
  if (len == 0 || len >= n || strcmp(p + len, leaf) != 0) {
    return 0;
  }
  memcpy(pid, p, len);
  pid[len] = '\0';
  return 1;
}

static ssize_t fixup(const char *path, char *buf, size_t n, ssize_t r) {
  char pid[24];
  if (r < 0 || !proc_pid(path, "/exe", pid, sizeof(pid)) ||
      !memmem(buf, (size_t)r, "qemu", 4)) {
    return r;
  }

  char name[64];
  strcpy(name, "/proc/");
  strcat(name, pid);
  strcat(name, "/cmdline");
  int fd = open(name, O_RDONLY);
  if (fd < 0) {
    return r;
  }
  char cmd[PATH_MAX + 64];
  ssize_t len = read(fd, cmd, sizeof(cmd) - 1);
  close(fd);
  if (len <= 0) {
    return r;
  }
  cmd[len] = '\0';

  /* Words are NUL separated; word 0 is the qemu interpreter. */
  const char *prog = NULL;
  for (const char *w = cmd + strlen(cmd) + 1; w < cmd + len; w += strlen(w) + 1) {
    if (w[0] != '-') {
      prog = w;
      break;
    }
  }
  if (prog == NULL) {
    return r;
  }

  char abs[PATH_MAX];
  if (prog[0] == '/') {
    strncpy(abs, prog, sizeof(abs) - 1);
    abs[sizeof(abs) - 1] = '\0';
  } else {
    strcpy(name, "/proc/");
    strcat(name, pid);
    strcat(name, "/cwd");
    ssize_t c = raw_readlinkat(AT_FDCWD, name, abs, sizeof(abs) - 2);
    if (c < 0) {
      return r;
    }
    abs[c] = '\0';
    strncat(abs, "/", sizeof(abs) - strlen(abs) - 1);
    strncat(abs, prog, sizeof(abs) - strlen(abs) - 1);
  }

  size_t out = strlen(abs);
  if (out > n) {
    out = n;
  }
  memcpy(buf, abs, out);
  return (ssize_t)out;
}

ssize_t readlink(const char *path, char *buf, size_t n) {
  return fixup(path, buf, n, raw_readlinkat(AT_FDCWD, path, buf, n));
}

ssize_t readlinkat(int fd, const char *path, char *buf, size_t n) {
  return fixup(path, buf, n, raw_readlinkat(fd, path, buf, n));
}

/* _FORTIFY_SOURCE variants used by Debian builds. */
ssize_t __readlink_chk(const char *path, char *buf, size_t n, size_t buflen) {
  (void)buflen;
  return readlink(path, buf, n);
}

ssize_t __readlinkat_chk(int fd, const char *path, char *buf, size_t n,
                         size_t buflen) {
  (void)buflen;
  return readlinkat(fd, path, buf, n);
}
