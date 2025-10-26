#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include <cstring>
struct Crypto {
    Crypto();
    ~Crypto();
    Crypto(const Crypto&) = delete;
    Crypto& operator=(const Crypto&) = delete;
    Crypto(Crypto&&) = delete;
    Crypto& operator=(Crypto&&) = delete;

    bool encrypt(const std::vector<uint8_t>& in,
                 std::vector<uint8_t>& out,
                 std::vector<uint8_t>& iv,
                 std::vector<uint8_t>& tag);

    bool decrypt(const std::vector<uint8_t>& in,
                 const std::vector<uint8_t>& iv,
                 const std::vector<uint8_t>& tag,
                 std::vector<uint8_t>& out);

private:
    uint8_t* key_{nullptr};
    size_t   key_len_{0};
    size_t   alloc_len_{0};
};
