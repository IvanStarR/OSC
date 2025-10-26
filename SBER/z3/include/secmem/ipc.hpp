#pragma once
#include <string>
#include <cstdint>
#include <vector>
#include <optional>

namespace secmem {
struct Peer {
  uint32_t uid;
  uint32_t gid;
  pid_t pid;
};

int server_listen(const std::string& path);
std::optional<Peer> get_peer(int fd);
bool send_memfd(int sock, int memfd);
int create_sealed_memfd(const std::string& name, const std::vector<uint8_t>& data);
}
