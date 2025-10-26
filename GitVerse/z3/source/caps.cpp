#include "gitproc/caps.hpp"
#include <cerrno>
#include <cstring>
#include <grp.h>
#include <pwd.h>
#include <string>
#include <sys/prctl.h>
#include <unistd.h>
#include <vector>

namespace gitproc {

bool drop_privileges(const std::string &run_as_user,
                     const std::vector<std::string> & /*drop_caps*/) {
  prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
  if (run_as_user.empty())
    return true;

  struct passwd *pw = getpwnam(run_as_user.c_str());
  if (!pw)
    return false;

  if (initgroups(pw->pw_name, pw->pw_gid) != 0)
    return false;
  if (setgid(pw->pw_gid) != 0)
    return false;
  if (setuid(pw->pw_uid) != 0)
    return false;

  return true;
}

} // namespace gitproc
