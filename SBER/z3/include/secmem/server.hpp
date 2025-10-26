#pragma once
#include "common.hpp"
#include "crypto.hpp"
#include "metrics.hpp"
#include "proto.hpp"
#include "storage.hpp"
#include <atomic>
#include <set>
#include <string>
#include <thread>

class Server {
    std::string sock_path;
    int listen_fd{-1};
    std::set<uid_t> allowed_uids;
    std::set<gid_t> allowed_gids;
    Storage& st;
    std::atomic<bool> stop_{false};
    secmem::Metrics metrics_;
    std::thread metrics_thread_;
    std::atomic<bool> metrics_stop_{false};
public:
    Server(const std::string& path, Storage& s);
    ~Server();
    void add_allowed_uid(uid_t u);
    void add_allowed_gid(gid_t g);
    bool start();
    void run();
    void shutdown();
};
