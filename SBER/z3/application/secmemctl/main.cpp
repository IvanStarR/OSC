#include "secmem/common.hpp"
#include "secmem/proto.hpp"
#include <iostream>

static int connect_sock(const std::string& path){
    int fd = socket(AF_UNIX, SOCK_STREAM|SOCK_CLOEXEC, 0);
    sockaddr_un addr{}; addr.sun_family=AF_UNIX; strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path)-1);
    if(connect(fd, (sockaddr*)&addr, sizeof(addr))<0){ return -1; }
    return fd;
}
int main(int argc, char** argv){
    if(argc<2){ std::cerr<<"usage: secmemctl --socket PATH put|get|del|list\n"; return 1; }
    std::string sock="/tmp/secmem.sock";
    int i=1;
    if(i<argc && std::string(argv[i])=="--socket" && i+1<argc){ sock=argv[i+1]; i+=2; }
    if(i>=argc) return 1;
    std::string cmd=argv[i++];
    int fd = connect_sock(sock);
    if(fd<0){ std::cerr<<"connect fail\n"; return 1; }
    if(cmd=="put"){
        if(i+1>=argc){ std::cerr<<"put KEY VALUE [--ttl SEC]\n"; return 1; }
        std::string key=argv[i++], val=argv[i++];
        uint32_t ttl=0;
        if(i<argc && std::string(argv[i])=="--ttl" && i+1<argc){ ttl=(uint32_t)std::stoul(argv[i+1]); i+=2; }
        MsgHdr h{OP_PUT,(uint32_t)key.size(),(uint32_t)val.size(),ttl};
        send(fd,&h,sizeof(h),0);
        send(fd,key.data(),key.size(),0);
        send(fd,val.data(),val.size(),0);
        RespHdr rh{};
        if(recv(fd,&rh,sizeof(rh),MSG_WAITALL)!=(ssize_t)sizeof(rh)) return 1;
        std::cout<<(rh.code==0?"OK\n":"ERR\n");
    } else if(cmd=="get"){
        if(i>=argc){ std::cerr<<"get KEY\n"; return 1; }
        std::string key=argv[i++];
        MsgHdr h{OP_GET,(uint32_t)key.size(),0,0};
        send(fd,&h,sizeof(h),0);
        send(fd,key.data(),key.size(),0);
        RespHdr rh{};
        if(recv(fd,&rh,sizeof(rh),MSG_WAITALL)!=(ssize_t)sizeof(rh)) return 1;
        if(rh.code!=0 || rh.n!=1){ std::cout<<"ERR\n"; return 1; }
        uint32_t dummy=0;
        auto fdo = recv_fd(fd, &dummy, sizeof(dummy));
        if(!fdo){ std::cout<<"ERR\n"; return 1; }
        int mfd=*fdo;
        off_t sz = lseek(mfd, 0, SEEK_END);
        lseek(mfd, 0, SEEK_SET);
        std::string s; s.resize(sz);
        ssize_t off=0; while(off<sz){ ssize_t n=read(mfd, s.data()+off, sz-off); if(n<=0) break; off+=n; }
        close(mfd);
        std::cout<<s;
        if(s.empty() || s.back()!='\n') std::cout<<"\n";
    } else if(cmd=="del"){
        if(i>=argc){ std::cerr<<"del KEY\n"; return 1; }
        std::string key=argv[i++];
        MsgHdr h{OP_DEL,(uint32_t)key.size(),0,0};
        send(fd,&h,sizeof(h),0);
        send(fd,key.data(),key.size(),0);
        RespHdr rh{};
        if(recv(fd,&rh,sizeof(rh),MSG_WAITALL)!=(ssize_t)sizeof(rh)) return 1;
        std::cout<<(rh.code==0?"OK\n":"ERR\n");
    } else if(cmd=="list"){
        MsgHdr h{OP_LIST,0,0,0};
        send(fd,&h,sizeof(h),0);
        RespHdr rh{};
        if(recv(fd,&rh,sizeof(rh),MSG_WAITALL)!=(ssize_t)sizeof(rh)) return 1;
        for(uint32_t k=0;k<rh.n;k++){
            uint32_t len; if(recv(fd,&len,sizeof(len),MSG_WAITALL)!=(ssize_t)sizeof(len)) return 1;
            std::string s; s.resize(len);
            if(recv(fd,s.data(),len,MSG_WAITALL)!=(ssize_t)len) return 1;
            std::cout<<s<<"\n";
        }
    } else if(cmd=="metrics"){
        MsgHdr h{OP_METRICS,0,0,0};
        send(fd,&h,sizeof(h),0);
        RespHdr rh{};
        if(recv(fd,&rh,sizeof(rh),MSG_WAITALL)!=(ssize_t)sizeof(rh)) return 1;
        std::string s; s.resize(rh.n);
        if(rh.n){ recv(fd, s.data(), rh.n, MSG_WAITALL); }
        std::cout<<s;
    } else {
        std::cerr<<"unknown\n";
        return 1;
    }
    close(fd);
    return 0;
}
