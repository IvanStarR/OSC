#include "secmem/server.hpp"
#include "secmem/storage.hpp"
#include "secmem/crypto.hpp"
#include <sys/resource.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <linux/prctl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <iostream>

int main(int argc, char** argv){
    std::string sock="/tmp/secmem.sock";
    std::vector<uid_t> uids;
    std::vector<gid_t> gids;
    uint32_t default_ttl=0;
    for(int i=1;i<argc;i++){
        std::string a=argv[i];
        if(a=="--socket" && i+1<argc) sock=argv[++i];
        else if(a=="--allow-uid" && i+1<argc) uids.push_back((uid_t)std::stoul(argv[++i]));
        else if(a=="--allow-gid" && i+1<argc) gids.push_back((gid_t)std::stoul(argv[++i]));
        else if(a=="--default-ttl" && i+1<argc) default_ttl=(uint32_t)std::stoul(argv[++i]);
    }

    umask(0077);

    rlimit rl{};
    getrlimit(RLIMIT_MEMLOCK, &rl);

    if(mlockall(MCL_CURRENT|MCL_FUTURE)!=0){
        std::cerr<<"mlockall failed: "<<strerror(errno)<<"\n";
    }

    prctl(PR_SET_DUMPABLE, 0, 0, 0, 0);
    prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);

    Crypto crypto;
    Storage st(&crypto, default_ttl);
    Server srv(sock, st);
    for(auto u: uids) srv.add_allowed_uid(u);
    for(auto g: gids) srv.add_allowed_gid(g);
    if(!srv.start()){
        std::cerr<<"failed to start\n";
        return 1;
    }
    srv.run();
    return 0;
}
