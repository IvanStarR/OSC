#include "secmem/storage.hpp"
#include <filesystem>

Storage::Storage(Crypto* c, uint32_t default_ttl): default_ttl_sec(default_ttl), crypto_(c){}
bool Storage::put(const std::string& key, const std::vector<uint8_t>& val, uint32_t ttl_sec, uid_t owner){
    std::vector<uint8_t> ciph, iv, tag;
    if(!crypto_->encrypt(val, ciph, iv, tag)) return false;
    int fd = memfd_create_compat(("sec."+key).c_str(), MFD_CLOEXEC);
    if(fd<0) return false;
    if(ftruncate(fd, ciph.size())!=0){ close(fd); return false; }
    ssize_t off=0; const uint8_t* p=ciph.data(); ssize_t left=(ssize_t)ciph.size();
    while(left>0){ ssize_t n=write(fd, p+off, left); if(n<=0){ close(fd); return false; } off+=n; left-=n; }
    seal_fd(fd);
    std::lock_guard lk(mu);
    auto it = m.find(key);
    if(it!=m.end()){ close(it->second.ciph_fd); }
    SecretRec r; r.ciph_fd=fd; r.ciph_len=(uint32_t)ciph.size(); r.iv=iv; r.tag=tag; r.owner_uid=owner;
    uint32_t t = ttl_sec?ttl_sec:default_ttl_sec;
    r.expires_ms = t? now_ms() + (uint64_t)t*1000 : 0;
    m[key]=std::move(r);
    return true;
}
bool Storage::put(const std::string& key, const std::vector<uint8_t>& val, std::chrono::seconds ttl){
    return put(key, val, (uint32_t)ttl.count(), 0);
}
std::optional<std::vector<uint8_t>> Storage::decrypt_for_send(const std::string& key){
    std::lock_guard lk(mu);
    auto it = m.find(key);
    if(it==m.end()) return std::nullopt;
    if(it->second.expires_ms && now_ms()>it->second.expires_ms) return std::nullopt;
    std::vector<uint8_t> ciph(it->second.ciph_len);
    if(lseek(it->second.ciph_fd, 0, SEEK_SET)<0) return std::nullopt;
    ssize_t off=0; while(off<(ssize_t)ciph.size()){ ssize_t n=read(it->second.ciph_fd, ciph.data()+off, ciph.size()-off); if(n<=0) return std::nullopt; off+=n; }
    std::vector<uint8_t> plain;
    if(!crypto_->decrypt(ciph, it->second.iv, it->second.tag, plain)) return std::nullopt;
    return plain;
}
bool Storage::get_plain_memfd(const std::string& key, int& fd_out, uid_t req_uid){
    std::lock_guard lk(mu);
    auto it = m.find(key);
    if(it==m.end()) return false;
    if(it->second.owner_uid!=0 && it->second.owner_uid!=req_uid) return false;
    if(it->second.expires_ms && now_ms()>it->second.expires_ms) return false;
    std::vector<uint8_t> ciph(it->second.ciph_len);
    if(lseek(it->second.ciph_fd, 0, SEEK_SET)<0) return false;
    ssize_t off=0; while(off<(ssize_t)ciph.size()){ ssize_t n=read(it->second.ciph_fd, ciph.data()+off, ciph.size()-off); if(n<=0) return false; off+=n; }
    std::vector<uint8_t> plain;
    if(!crypto_->decrypt(ciph, it->second.iv, it->second.tag, plain)) return false;
    int fd = memfd_create_compat(("plain."+key).c_str(), MFD_CLOEXEC);
    if(fd<0) return false;
    if(ftruncate(fd, plain.size())!=0){ close(fd); return false; }
    off=0; while(off<(ssize_t)plain.size()){ ssize_t n=write(fd, plain.data()+off, plain.size()-off); if(n<=0){ close(fd); return false; } off+=n; }
    seal_fd(fd);
    fd_out=fd;
    return true;
}
bool Storage::del(const std::string& key, uid_t req_uid){
    std::lock_guard lk(mu);
    auto it = m.find(key);
    if(it==m.end()) return false;
    if(it->second.owner_uid!=0 && it->second.owner_uid!=req_uid) return false;
    close(it->second.ciph_fd);
    m.erase(it);
    return true;
}
bool Storage::del(const std::string& key){
    std::lock_guard lk(mu);
    auto it = m.find(key);
    if(it==m.end()) return false;
    close(it->second.ciph_fd);
    m.erase(it);
    return true;
}
std::vector<std::string> Storage::list(uid_t req_uid){
    std::lock_guard lk(mu);
    std::vector<std::string> out;
    for(auto& [k,v]: m){
        if(v.owner_uid==0 || v.owner_uid==req_uid){
            if(!v.expires_ms || now_ms()<=v.expires_ms) out.push_back(k);
        }
    }
    return out;
}
std::vector<std::string> Storage::snapshot_keys() const{
    std::vector<std::string> out;
    std::lock_guard lk(const_cast<std::mutex&>(mu));
    for(auto& [k,v]: const_cast<std::map<std::string,SecretRec>&>(m)) out.push_back(k);
    return out;
}
size_t Storage::size() const{
    std::lock_guard lk(const_cast<std::mutex&>(mu));
    return m.size();
}
void Storage::start_sweeper(){
    stop_=false;
    sweeper=std::thread([this]{ sweep(); });
}
void Storage::stop(){
    stop_=true;
    if(sweeper.joinable()) sweeper.join();
    std::lock_guard lk(mu);
    for(auto& [k,v]: m){ close(v.ciph_fd); }
    m.clear();
}
void Storage::sweep(){
    while(!stop_){
        {
            std::lock_guard lk(mu);
            uint64_t t=now_ms();
            std::vector<std::string> dead;
            for(auto& [k,v]: m){ if(v.expires_ms && t>v.expires_ms) dead.push_back(k); }
            for(auto& k: dead){ close(m[k].ciph_fd); m.erase(k); }
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}
