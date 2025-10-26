#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <errno.h>
#include <fcntl.h>
#include <linux/memfd.h>
#include <map>
#include <mutex>
#include <openssl/evp.h>
#include <optional>
#include <set>
#include <signal.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <sstream>
#include <string.h>
#include <string>
#include <string_view>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/un.h>
#include <thread>
#include <time.h>
#include <unistd.h>
#include <vector>

inline int memfd_create_compat(const char *name, unsigned int flags) {
  return syscall(SYS_memfd_create, name, flags);
}
inline void seal_fd(int fd) {
  int seals = F_SEAL_SEAL | F_SEAL_GROW | F_SEAL_SHRINK | F_SEAL_WRITE;
  fcntl(fd, F_ADD_SEALS, seals);
}
inline bool send_fd(int sock, int fd, const void *data, size_t len) {
  struct iovec iov {
    const_cast<void *>(data), len
  };
  char cmsgbuf[CMSG_SPACE(sizeof(int))];
  struct msghdr msg {};
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;
  msg.msg_control = cmsgbuf;
  msg.msg_controllen = sizeof(cmsgbuf);
  struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
  cmsg->cmsg_level = SOL_SOCKET;
  cmsg->cmsg_type = SCM_RIGHTS;
  cmsg->cmsg_len = CMSG_LEN(sizeof(int));
  memcpy(CMSG_DATA(cmsg), &fd, sizeof(int));
  msg.msg_controllen = CMSG_SPACE(sizeof(int));
  return sendmsg(sock, &msg, 0) == (ssize_t)len;
}
inline std::optional<int> recv_fd(int sock, void *data, size_t len) {
  struct iovec iov {
    data, len
  };
  char cmsgbuf[CMSG_SPACE(sizeof(int))];
  struct msghdr msg {};
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;
  msg.msg_control = cmsgbuf;
  msg.msg_controllen = sizeof(cmsgbuf);
  ssize_t n = recvmsg(sock, &msg, 0);
  if (n != (ssize_t)len)
    return std::nullopt;
  struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
  if (!cmsg || cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS)
    return std::nullopt;
  int fd;
  memcpy(&fd, CMSG_DATA(cmsg), sizeof(int));
  return fd;
}
struct Cred {
  uid_t uid;
  gid_t gid;
  pid_t pid;
};
inline std::optional<Cred> get_peer_cred(int fd) {
  struct ucred cr;
  socklen_t len = sizeof(cr);
  if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cr, &len) == 0)
    return Cred{cr.uid, cr.gid, cr.pid};
  return std::nullopt;
}
inline uint64_t now_ms() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(steady_clock::now().time_since_epoch())
      .count();
}
