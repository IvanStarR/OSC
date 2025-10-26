#include "secmem/server.hpp"
#include "secmem/common.hpp"
#include "secmem/storage.hpp"
#include <spdlog/spdlog.h>
#include <sys/stat.h>
#include <unistd.h>

static bool mkdir_p_mode(const std::string& path, mode_t mode){
    if(path.empty()) return true;
    size_t pos=1;
    while(true){
        pos = path.find('/', pos);
        std::string sub = path.substr(0,pos);
        if(!sub.empty()){
            struct stat st{};
            if(lstat(sub.c_str(), &st) != 0){
                if(::mkdir(sub.c_str(), mode) < 0 && errno != EEXIST) return false;
                ::chmod(sub.c_str(), mode);
            }
        }
        if(pos==std::string::npos) break;
        ++pos;
    }
    return true;
}

Server::Server(const std::string& path, Storage& s): sock_path(path), st(s){}
Server::~Server(){ shutdown(); }
void Server::add_allowed_uid(uid_t u){ allowed_uids.insert(u); }
void Server::add_allowed_gid(gid_t g){ allowed_gids.insert(g); }

bool Server::start(){
    size_t slash = sock_path.rfind('/');
    if(slash != std::string::npos){
        std::string dir = sock_path.substr(0, slash);
        if(!dir.empty()){
            if(!mkdir_p_mode(dir, 0700)) return false;
            ::chmod(dir.c_str(), 0700);
        }
    }
    ::unlink(sock_path.c_str());
    listen_fd = ::socket(AF_UNIX, SOCK_STREAM|SOCK_CLOEXEC, 0);
    if(listen_fd<0) return false;
    sockaddr_un addr{}; addr.sun_family=AF_UNIX;
    ::strncpy(addr.sun_path, sock_path.c_str(), sizeof(addr.sun_path)-1);
    if(::bind(listen_fd, (sockaddr*)&addr, sizeof(addr))<0) return false;
    ::chmod(sock_path.c_str(), 0600);
    if(::listen(listen_fd, 128)<0) return false;
    spdlog::info("listen {}", sock_path);
    metrics_stop_=false;
    metrics_thread_ = std::thread([this](){
        while(!metrics_stop_){
            metrics_.set_secrets_gauge(st.size());
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    });
    return true;
}

void Server::run(){
    st.start_sweeper();
    while(!stop_){
        int cfd = ::accept4(listen_fd, nullptr, nullptr, SOCK_CLOEXEC);
        if(cfd<0){ if(errno==EINTR) continue; break; }
        auto cred = get_peer_cred(cfd);
        if(!cred){ ::close(cfd); continue; }
        bool ok=true;
        if(!allowed_uids.empty() && allowed_uids.count(cred->uid)==0) ok=false;
        if(!ok && !allowed_gids.empty() && allowed_gids.count(cred->gid)>0) ok=true;
        if(!ok){
            spdlog::warn("reject uid={} gid={} pid={}", cred->uid, cred->gid, cred->pid);
            metrics_.inc_error("acl_reject");
            ::close(cfd); continue;
        }
        spdlog::info("accept uid={} gid={} pid={}", cred->uid, cred->gid, cred->pid);
        std::thread([this,cfd,uid=cred->uid](){
            for(;;){
                MsgHdr h{};
                ssize_t n = ::recv(cfd, &h, sizeof(h), MSG_WAITALL);
                if(n==0 || n<0) break;
                auto t0 = std::chrono::steady_clock::now();
                if(h.op==OP_PUT){
                    std::string key(h.klen, '\0');
                    std::vector<uint8_t> val(h.vlen);
                    if(::recv(cfd, key.data(), key.size(), MSG_WAITALL)!=(ssize_t)key.size()) break;
                    if(::recv(cfd, val.data(), val.size(), MSG_WAITALL)!=(ssize_t)val.size()) break;
                    bool r = st.put(key, val, h.ttl, uid);
                    RespHdr rh{ r?0u:1u, 0u };
                    ::send(cfd, &rh, sizeof(rh), 0);
                    auto dt = std::chrono::duration<double>(std::chrono::steady_clock::now()-t0).count();
                    if(r){ metrics_.inc_op("put"); metrics_.observe_latency("put", dt); }
                    else { metrics_.inc_error("put"); }
                    spdlog::info("put uid={} key={} ttl={}", uid, key, h.ttl);
                } else if(h.op==OP_GET){
                    std::string key(h.klen, '\0');
                    if(::recv(cfd, key.data(), key.size(), MSG_WAITALL)!=(ssize_t)key.size()) break;
                    int fd;
                    bool r = st.get_plain_memfd(key, fd, uid);
                    RespHdr rh{ r?0u:1u, r?1u:0u };
                    if(!r){
                        ::send(cfd, &rh, sizeof(rh), 0);
                        metrics_.inc_error("get");
                        spdlog::warn("get fail uid={} key={}", uid, key);
                    } else{
                        ::send(cfd, &rh, sizeof(rh), 0);
                        uint32_t dummy=0;
                        send_fd(cfd, fd, &dummy, sizeof(dummy));
                        ::close(fd);
                        auto dt = std::chrono::duration<double>(std::chrono::steady_clock::now()-t0).count();
                        metrics_.inc_op("get");
                        metrics_.observe_latency("get", dt);
                        spdlog::info("get uid={} key={}", uid, key);
                    }
                } else if(h.op==OP_DEL){
                    std::string key(h.klen, '\0');
                    if(::recv(cfd, key.data(), key.size(), MSG_WAITALL)!=(ssize_t)key.size()) break;
                    bool r = st.del(key, uid);
                    RespHdr rh{ r?0u:1u, 0u };
                    ::send(cfd, &rh, sizeof(rh), 0);
                    auto dt = std::chrono::duration<double>(std::chrono::steady_clock::now()-t0).count();
                    if(r){ metrics_.inc_op("del"); metrics_.observe_latency("del", dt); }
                    else { metrics_.inc_error("del"); }
                    spdlog::info("del uid={} key={} rc={}", uid, key, r?0:1);
                } else if(h.op==OP_LIST){
                    auto v = st.list(uid);
                    RespHdr rh{0u, (uint32_t)v.size()};
                    ::send(cfd, &rh, sizeof(rh), 0);
                    for(auto& s: v){
                        uint32_t len = (uint32_t)s.size();
                        ::send(cfd, &len, sizeof(len), 0);
                        ::send(cfd, s.data(), s.size(), 0);
                    }
                    metrics_.inc_op("list");
                    spdlog::info("list uid={} n={}", uid, v.size());
                } else if(h.op==OP_METRICS){
                    metrics_.set_secrets_gauge(st.size());
                    std::string txt = metrics_.render_prometheus();
                    RespHdr rh{0u, (uint32_t)txt.size()};
                    ::send(cfd, &rh, sizeof(rh), 0);
                    if(!txt.empty()) ::send(cfd, txt.data(), txt.size(), 0);
                } else {
                    metrics_.inc_error("bad_op");
                    break;
                }
            }
            ::close(cfd);
        }).detach();
    }
}

void Server::shutdown(){
    stop_=true;
    if(listen_fd>=0){ ::close(listen_fd); listen_fd=-1; }
    ::unlink(sock_path.c_str());
    metrics_stop_=true;
    if(metrics_thread_.joinable()) metrics_thread_.join();
    st.stop();
}
