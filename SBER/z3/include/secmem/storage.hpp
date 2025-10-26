#pragma once
#include "common.hpp"
#include "crypto.hpp"
struct SecretRec {
    int ciph_fd;
    uint32_t ciph_len;
    std::vector<uint8_t> iv;
    std::vector<uint8_t> tag;
    uint64_t expires_ms;
    uid_t owner_uid;
};
class Storage {
    std::map<std::string, SecretRec> m;
    std::mutex mu;
    std::atomic<bool> stop_{false};
    std::thread sweeper;
    uint32_t default_ttl_sec;
    Crypto* crypto_;
public:
    Storage(Crypto* c, uint32_t default_ttl=0);
    bool put(const std::string& key, const std::vector<uint8_t>& val, uint32_t ttl_sec, uid_t owner);
    bool put(const std::string& key, const std::vector<uint8_t>& val, std::chrono::seconds ttl);
    std::optional<std::vector<uint8_t>> decrypt_for_send(const std::string& key);
    bool get_plain_memfd(const std::string& key, int& fd_out, uid_t req_uid);
    bool del(const std::string& key, uid_t req_uid);
    bool del(const std::string& key);
    std::vector<std::string> list(uid_t req_uid);
    std::vector<std::string> snapshot_keys() const;
    size_t size() const;
    void start_sweeper();
    void stop();
    void sweep();
};
