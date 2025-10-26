#include "secmem/ipc.hpp"
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <linux/memfd.h>
#ifndef MFD_ALLOW_SEALING
#define MFD_ALLOW_SEALING 0x0002
#endif
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <string>

namespace secmem {

static bool mkdir_p(const std::string& path, mode_t mode){
  size_t pos = 1;
  while(true){
    pos = path.find('/', pos);
    std::string sub = path.substr(0, pos);
    if(!sub.empty()){
      if(::mkdir(sub.c_str(), mode) < 0 && errno != EEXIST) return false;
    }
    if(pos == std::string::npos) break;
    ++pos;
  }
  return true;
}

int server_listen(const std::string& path){
  int fd = socket(AF_UNIX, SOCK_STREAM|SOCK_CLOEXEC, 0);
  if(fd<0) return -1;

  size_t slash = path.rfind('/');
  if(slash != std::string::npos){
    std::string dir = path.substr(0, slash);
    if(!dir.empty() && access(dir.c_str(), W_OK) != 0){
      if(!mkdir_p(dir, 0700)){ close(fd); return -1; }
    }
  }

  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  unlink(path.c_str());
  strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path)-1);
  if(bind(fd, (sockaddr*)&addr, sizeof(addr))<0){ close(fd); return -1; }
  if(chmod(path.c_str(), 0700)<0){ close(fd); return -1; }
  if(listen(fd, 128)<0){ close(fd); return -1; }
  return fd;
}

std::optional<Peer> get_peer(int fd){
#if defined(SO_PEERCRED)
  ucred c{};
  socklen_t l = sizeof(c);
  if(getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &c, &l)<0) return std::nullopt;
  Peer p{c.uid, c.gid, c.pid};
  return p;
#else
  return std::nullopt;
#endif
}

static int memfd_create_compat(const char* name, unsigned int flags){
#ifdef MFD_CLOEXEC
  return syscall(SYS_memfd_create, name, flags);
#else
  (void)name; (void)flags;
  return -1;
#endif
}

int create_sealed_memfd(const std::string& name, const std::vector<uint8_t>& data){
  int fd = memfd_create_compat(name.c_str(), MFD_CLOEXEC | MFD_ALLOW_SEALING);
  if(fd<0) return -1;

  if(!data.empty()){
    if(ftruncate(fd, (off_t)data.size())<0){ close(fd); return -1; }
    ssize_t off = 0;
    while(off < (ssize_t)data.size()){
      ssize_t w = pwrite(fd, data.data()+off, data.size()-off, off);
      if(w<=0){ close(fd); return -1; }
      off += w;
    }
  }

#ifdef F_ADD_SEALS
  int seals = F_SEAL_SEAL|F_SEAL_GROW|F_SEAL_SHRINK|F_SEAL_WRITE;
  if(fcntl(fd, F_ADD_SEALS, seals)<0){
    if(errno!=EPERM && errno!=EINVAL && errno!=ENOSYS){
      close(fd);
      return -1;
    }
    // иначе — ядро/FS не поддерживает SEAL, отправим без печатей
  }
#endif
  return fd;
}

bool send_memfd(int sock, int memfd){
  struct msghdr msg{};
  char c = 'X';
  struct iovec iov{&c,1};
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;

  char cmsgbuf[CMSG_SPACE(sizeof(int))];
  msg.msg_control = cmsgbuf;
  msg.msg_controllen = sizeof(cmsgbuf);

  struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
  cmsg->cmsg_level = SOL_SOCKET;
  cmsg->cmsg_type = SCM_RIGHTS;
  cmsg->cmsg_len = CMSG_LEN(sizeof(int));
  memcpy(CMSG_DATA(cmsg), &memfd, sizeof(int));
  msg.msg_controllen = CMSG_SPACE(sizeof(int));

  return sendmsg(sock, &msg, 0) == 1;
}
}
