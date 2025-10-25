#include "gitproc/caps.hpp"
#include <pwd.h>
#include <grp.h>
#include <sys/prctl.h>
#include <unistd.h>
#include <vector>
#include <string>
#include <cerrno>
#include <cstring>

namespace gitproc {

bool drop_privileges(const std::string& run_as_user, const std::vector<std::string>& /*drop_caps*/) {
    // Best-effort: PR_SET_NO_NEW_PRIVS и смена uid/gid если задан пользователь
    prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
    if (run_as_user.empty()) return true;

    struct passwd* pw = getpwnam(run_as_user.c_str());
    if (!pw) return false;

    if (initgroups(pw->pw_name, pw->pw_gid) != 0) return false;
    if (setgid(pw->pw_gid) != 0) return false;
    if (setuid(pw->pw_uid) != 0) return false;

    // Без libcap не можем полноценно управлять набором capabilities.
    // Но при смене uid на непривилегированного набор и так будет урезан.
    return true;
}

} // namespace gitproc
