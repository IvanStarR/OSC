#include "secmem/agent.hpp"
#include "secmem/ipc.hpp"
#include "secmem/crypto.hpp"
#include "secmem/storage.hpp"
#include <spdlog/spdlog.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <thread>
#include <atomic>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <cstring>
#include <unistd.h>
#include <errno.h>

namespace secmem {
namespace { enum Op : uint8_t { OP_PUT=1, OP_GET=2, OP_DEL=3, OP_LIST=4 }; }

struct Agent::Impl {
  AgentConfig cfg; Crypto crypto; Storage store; int lsock{-1}; std::atomic<bool> stop{false};
  Impl(AgentConfig c): cfg(std::move(c)), store(&crypto) {}
};

static bool check_mlock(){ return mlockall(MCL_CURRENT|MCL_FUTURE)==0; }
static bool allowed(const AgentConfig& cfg, const Peer& p){
  if(!cfg.allowed_uids.empty() && !cfg.allowed_uids.count(p.uid)) return false;
  if(!cfg.allowed_gids.empty() && !cfg.allowed_gids.count(p.gid)) return false;
  return true;
}
static bool read_n(int fd, void* buf, size_t n){
  size_t off=0; char* p=(char*)buf;
  while(off<n){ ssize_t r = recv(fd, p+off, n-off, MSG_NOSIGNAL); if(r<=0) return false; off += r; }
  return true;
}
static bool write_n(int fd, const void* buf, size_t n){
  size_t off=0; const char* p=(const char*)buf;
  while(off<n){ ssize_t w = send(fd, p+off, n-off, MSG_NOSIGNAL); if(w<=0) return false; off += w; }
  return true;
}

Agent::Agent(AgentConfig cfg): impl_(std::make_unique<Impl>(std::move(cfg))){}
Agent::~Agent() = default;

int Agent::run(){
  if(!check_mlock()){ spdlog::error("mlockall failed"); return 1; }
  impl_->lsock = server_listen(impl_->cfg.socket_path);
  if(impl_->lsock<0){ spdlog::error("listen failed"); return 1; }

  if(geteuid()==0 && !impl_->cfg.allowed_uids.empty()){
    uint32_t target = *impl_->cfg.allowed_uids.begin();
    chown(impl_->cfg.socket_path.c_str(), target, (gid_t)-1);
    chmod(impl_->cfg.socket_path.c_str(), 0600);
  }

  spdlog::info("listening on {}", impl_->cfg.socket_path);

  std::thread sweeper([this](){
    while(!impl_->stop.load()){
      impl_->store.sweep();
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
  });

  while(!impl_->stop.load()){
    int cs = accept4(impl_->lsock, nullptr, nullptr, SOCK_CLOEXEC);
    if(cs<0){
      if(errno==EINTR) continue;
      if(errno==EBADF) break;
      spdlog::warn("accept failed: {}", strerror(errno));
      break;
    }
    auto peer = get_peer(cs);
    if(!peer){ close(cs); continue; }
    spdlog::info("accept uid={} pid={}", peer->uid, peer->pid);
    if(!allowed(impl_->cfg, *peer)){ spdlog::info("reject uid={}", peer->uid); close(cs); continue; }

    std::thread([this, cs, pr=*peer](){
      uint8_t op;
      if(!read_n(cs, &op, 1)){ close(cs); return; }
      if(op==OP_PUT){
        uint32_t klen,vlen,ttl;
        if(!read_n(cs,&klen,4) || !read_n(cs,&vlen,4) || !read_n(cs,&ttl,4)){ close(cs); return; }
        std::string key(klen,'\0'); std::vector<uint8_t> val(vlen);
        if(!read_n(cs,key.data(),klen) || !read_n(cs,val.data(),vlen)){ close(cs); return; }
        bool ok = impl_->store.put(key, val, std::chrono::seconds(ttl?ttl:impl_->cfg.default_ttl.count()));
        uint8_t rc = ok?0:1; write_n(cs,&rc,1);
        spdlog::info("uid={} put key={} ttl={}", pr.uid, key, ttl);
      } else if(op==OP_GET){
        uint32_t klen; if(!read_n(cs,&klen,4)){ close(cs); return; }
        std::string key(klen,'\0'); if(!read_n(cs,key.data(),klen)){ close(cs); return; }
        auto plain = impl_->store.decrypt_for_send(key);
        if(!plain){
          uint8_t rc=1; write_n(cs,&rc,1);
          spdlog::warn("uid={} get FAIL key={} (decrypt or expired)", pr.uid, key);
          close(cs); return;
        }
        int mfd = create_sealed_memfd("secmem", *plain);
        if(mfd<0){ uint8_t rc=2; write_n(cs,&rc,1); spdlog::warn("uid={} memfd fail key={}", pr.uid, key); close(cs); return; }
        uint8_t rc=0; write_n(cs,&rc,1); send_memfd(cs, mfd); close(mfd);
        spdlog::info("uid={} get key={}", pr.uid, key);
      } else if(op==OP_DEL){
        uint32_t klen; if(!read_n(cs,&klen,4)){ close(cs); return; }
        std::string key(klen,'\0'); if(!read_n(cs,key.data(),klen)){ close(cs); return; }
        bool ok = impl_->store.del(key); uint8_t rc = ok?0:1; write_n(cs,&rc,1);
        spdlog::info("uid={} del key={}", pr.uid, key);
      } else if(op==OP_LIST){
        auto keys = impl_->store.snapshot_keys();
        uint32_t n = (uint32_t)keys.size();
        write_n(cs,&n,4);
        for(auto& k: keys){
          uint32_t klen = (uint32_t)k.size();
          write_n(cs,&klen,4);
          write_n(cs,k.data(),klen);
        }
        spdlog::info("uid={} list n={}", pr.uid, n);
      } else {
        uint8_t rc=0xFF; write_n(cs,&rc,1);
      }
      close(cs);
    }).detach();
  }

  impl_->stop.store(true);
  if(impl_->lsock>=0){ close(impl_->lsock); impl_->lsock = -1; }
  sweeper.join();
  return 0;
}
}
