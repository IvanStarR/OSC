#include "secmem/crypto.hpp"
#include "secmem/secure.hpp"
#include <openssl/evp.h>
#include <openssl/crypto.h>
#include <sys/mman.h>
#include <unistd.h>
#include <random>

static size_t page_size() {
    long ps = sysconf(_SC_PAGESIZE);
    return ps > 0 ? (size_t)ps : 4096u;
}
static size_t round_up(size_t n, size_t a) {
    return (n + a - 1) / a * a;
}

Crypto::Crypto() {
    key_len_ = 32;
    alloc_len_ = round_up(key_len_, page_size());
    void* p = mmap(nullptr, alloc_len_, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) {
        key_ = nullptr; key_len_ = 0; alloc_len_ = 0;
        return;
    }
    mlock(p, alloc_len_);
    key_ = static_cast<uint8_t*>(p);

    std::random_device rd;
    for (size_t i = 0; i < key_len_; ++i) key_[i] = static_cast<uint8_t>(rd());
    if (alloc_len_ > key_len_) {
        memset(key_ + key_len_, 0, alloc_len_ - key_len_);
    }
}

Crypto::~Crypto() {
    if (key_) {
        OPENSSL_cleanse(key_, key_len_);
        munlock(key_, alloc_len_);
        munmap(key_, alloc_len_);
        key_ = nullptr;
    }
    key_len_ = 0;
    alloc_len_ = 0;
}

bool Crypto::encrypt(const std::vector<uint8_t>& in,
                     std::vector<uint8_t>& out,
                     std::vector<uint8_t>& iv,
                     std::vector<uint8_t>& tag) {
    if (!key_ || key_len_ != 32) return false;

    iv.resize(12);
    std::random_device rd;
    for (size_t i = 0; i < iv.size(); ++i) iv[i] = static_cast<uint8_t>(rd());

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return false;

    if (1 != EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr)) {
        EVP_CIPHER_CTX_free(ctx); return false;
    }
    if (1 != EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, (int)iv.size(), nullptr)) {
        EVP_CIPHER_CTX_free(ctx); return false;
    }
    if (1 != EVP_EncryptInit_ex(ctx, nullptr, nullptr, key_, iv.data())) {
        EVP_CIPHER_CTX_free(ctx); return false;
    }

    out.resize(in.size());
    int len = 0, outlen = 0;
    if (1 != EVP_EncryptUpdate(ctx, out.data(), &len, in.data(), (int)in.size())) {
        EVP_CIPHER_CTX_free(ctx); return false;
    }
    outlen = len;
    if (1 != EVP_EncryptFinal_ex(ctx, out.data() + outlen, &len)) {
        EVP_CIPHER_CTX_free(ctx); return false;
    }
    outlen += len;
    out.resize(outlen);

    tag.resize(16);
    if (1 != EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, tag.data())) {
        EVP_CIPHER_CTX_free(ctx); return false;
    }

    EVP_CIPHER_CTX_free(ctx);
    return true;
}

bool Crypto::decrypt(const std::vector<uint8_t>& in,
                     const std::vector<uint8_t>& iv,
                     const std::vector<uint8_t>& tag,
                     std::vector<uint8_t>& out) {
    if (!key_ || key_len_ != 32) return false;

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return false;

    if (1 != EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr)) {
        EVP_CIPHER_CTX_free(ctx); return false;
    }
    if (1 != EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, (int)iv.size(), nullptr)) {
        EVP_CIPHER_CTX_free(ctx); return false;
    }
    if (1 != EVP_DecryptInit_ex(ctx, nullptr, nullptr, key_, iv.data())) {
        EVP_CIPHER_CTX_free(ctx); return false;
    }

    out.resize(in.size());
    int len = 0, outlen = 0;
    if (1 != EVP_DecryptUpdate(ctx, out.data(), &len, in.data(), (int)in.size())) {
        EVP_CIPHER_CTX_free(ctx); return false;
    }
    outlen = len;

    if (1 != EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, const_cast<uint8_t*>(tag.data()))) {
        EVP_CIPHER_CTX_free(ctx); return false;
    }
    if (1 != EVP_DecryptFinal_ex(ctx, out.data() + outlen, &len)) {
        EVP_CIPHER_CTX_free(ctx); return false;
    }
    outlen += len;
    out.resize(outlen);

    EVP_CIPHER_CTX_free(ctx);
    return true;
}
