#include "exec.hpp"

#include <cstdio>
CmdResult run_cmd(const std::string& cmd) {
    CmdResult r {0, {}};
    std::string c = cmd + " 2>/dev/null";
    FILE* p = popen(c.c_str(), "r");
    if (!p) {
        r.code = 127;
        return r;
    }
    char buf[8192];
    while (size_t n = fread(buf, 1, sizeof(buf), p))
        r.out.append(buf, n);
    r.code = pclose(p);
    return r;
}